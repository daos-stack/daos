/*
 * (C) Copyright 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * Dual-provider client
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <semaphore.h>
#include <cart/api.h>
#include <cart/types.h>
#include <signal.h>

#include "crt_utils.h"
#include "dual_provider_common.h"

static int
g_do_shutdown;

static void *
progress_function(void *data)
{
	crt_context_t *p_ctx = (crt_context_t *)data;

	while (g_do_shutdown == 0)
		crt_progress(*p_ctx, 1000);

	crt_context_destroy(*p_ctx, 1);

	return NULL;
}

static void
rpc_handle_reply(const struct crt_cb_info *info)
{
	sem_t	*sem;

	D_ASSERTF(info->cci_rc == 0, "rpc response failed. rc: %d\n",
		  info->cci_rc);

	sem = (sem_t *)info->cci_arg;
	sem_post(sem);
}

int main(int argc, char **argv)
{
	crt_context_t		crt_ctx;
	crt_group_t		*grp;
	int			rc;
	sem_t			sem;
	pthread_t		progress_thread;
	crt_rpc_t		*rpc = NULL;
	struct RPC_PING_in	*input;
	crt_endpoint_t		server_ep;
	int			i;
	d_rank_list_t		*rank_list;
	d_rank_t		rank;
	int			tag;
	uint32_t		grp_size;
	char			c;
	char			*arg_interface = NULL;
	char			*arg_domain = NULL;
	char			*arg_provider = NULL;
	char			*arg_num_ctx = NULL;
	int			num_remote_tags;
	bool			use_primary = true;

	while ((c = getopt(argc, argv, "i:p:d:s")) != -1) {
		switch (c) {
		case 'i':
			arg_interface = optarg;
			break;
		case 'd':
			arg_domain = optarg;
			break;
		case 'p':
			arg_provider = optarg;
			break;
		case 'c':
			arg_num_ctx = optarg;
			break;
		case 's':
			use_primary = false;
			break;
		default:
			printf("Error: unknown option %c\n", c);
			return -1;
		}
	}

	if (use_primary)
		unsetenv("CRT_SECONDARY_PROVIDER");
	else
		setenv("CRT_SECONDARY_PROVIDER", "1", 1);

	rc = d_log_init();
	assert(rc == 0);
	/* rank, num_attach_retries, is_server, assert_on_error */
	crtu_test_init(0, 20, false, true);

	DBG_PRINT("Client starting up\n");

	rc = sem_init(&sem, 0, 0);
	if (rc != 0) {
		D_ERROR("sem_init() failed; rc=%d\n", rc);
		assert(0);
	}

	num_remote_tags = 1;
	if (arg_num_ctx != NULL)
		num_remote_tags = atoi(arg_num_ctx);

	DBG_PRINT("------------------------------------\n");
	DBG_PRINT("Provider: '%s' Interface: '%s'  Domain: '%s'\n",
		  arg_provider, arg_interface, arg_domain);
	DBG_PRINT("Number of remote tags: %d\n", num_remote_tags);
	DBG_PRINT("Primary_provider: %d\n", use_primary);
	DBG_PRINT("------------------------------------\n");
	crt_init_options_t init_opts = {0};

	init_opts.cio_provider = arg_provider;
	init_opts.cio_interface = arg_interface;
	init_opts.cio_domain = arg_domain;

	rc = crt_init_opt(NULL, 0, &init_opts);
	if (rc != 0) {
		D_ERROR("crt_init() failed; rc=%d\n", rc);
		assert(0);
	}

	rc = crt_proto_register(&my_proto_fmt);
	if (rc != 0) {
		D_ERROR("crt_proto_register() failed; rc=%d\n", rc);
		assert(0);
	}

	rc = crt_context_create(&crt_ctx);
	if (rc != 0) {
		D_ERROR("crt_context_create() failed; rc=%d\n", rc);
		assert(0);
	}

	rc = pthread_create(&progress_thread, 0,
			    progress_function, &crt_ctx);
	assert(rc == 0);

	int num_servers;

	rc = crt_group_view_create(SERVER_GROUP_NAME, &grp);
	if (rc != 0) {
		error_exit();
	}

	num_servers = 2;

	/* Parse /tmp/ files for uris. servers generate those */
	{
		FILE	*f;
		char	*filename;
		char	pri_uri0[255];
		char	sec_uri0[255];
		int	serv_rank;


		for (serv_rank = 0; serv_rank < num_servers; serv_rank++) {
			D_ASPRINTF(filename, "/tmp/%s_rank_%d_uris.cart",
				   SERVER_GROUP_NAME, serv_rank);
			if (filename == NULL)
				error_exit();

			f = fopen(filename, "r");
			if (f == NULL) {
				perror("failed: ");
				error_exit();
			}

			rc = fscanf(f, "%254s", pri_uri0);
			if (rc == EOF)
				error_exit();

			rc = fscanf(f, "%254s", sec_uri0);
			if (rc == EOF)
				error_exit();

			printf("server_rank=%d\n", serv_rank);
			printf("pri_uri=%s\n", pri_uri0);
			printf("sec_uri=%s\n", sec_uri0);

			printf("Using %s URIs for ranks\n",
			       (use_primary) ? "primary" : "secondary");
			rc = crt_group_primary_rank_add(crt_ctx, grp, serv_rank,
							(use_primary) ? pri_uri0 : sec_uri0);
			fclose(f);
			D_FREE(filename);
		}
	}

	/* Load group */
	rc = crt_group_size(grp, &grp_size);
	if (rc != 0) {
		D_ERROR("crt_group_size() failed; rc=%d\n", rc);
		assert(0);
	}

	rc = crt_group_ranks_get(grp, &rank_list);
	if (rc != 0) {
		D_ERROR("crt_group_ranks_get() failed; rc=%d\n", rc);
		assert(0);
	}

	DBG_PRINT("Group loaded, group size=%d\n", grp_size);
	if (rank_list->rl_nr != grp_size) {
		D_ERROR("rank_list differs in size. expected %d got %d\n",
			grp_size, rank_list->rl_nr);
		assert(0);
	}


	/* Cycle through all ranks and 8 tags and send rpc to each */
	for (i = 0; i < rank_list->rl_nr; i++) {

		rank = rank_list->rl_ranks[i];

		for (tag = 0; tag < num_remote_tags; tag++) {
			DBG_PRINT("Sending ping to %d:%d\n", rank, tag);

			server_ep.ep_rank = rank;
			server_ep.ep_tag = tag;
			server_ep.ep_grp = grp;

			rc = crt_req_create(crt_ctx, &server_ep,
					    RPC_PING, &rpc);
			if (rc != 0) {
				D_ERROR("crt_req_create() failed; rc=%d\n",
					rc);
				assert(0);
			}

			input = crt_req_get(rpc);

			input->size1 = 1024;
			input->size2 = 10;
			rc = crt_req_send(rpc, rpc_handle_reply, &sem);
			crtu_sem_timedwait(&sem, 10, __LINE__);
			DBG_PRINT("Ping response from %d:%d\n", rank, tag);
		}
	}


	/* Send shutdown RPC to each server */
	bool send_shutdown = false;


	if (send_shutdown) {
		for (i = 0; i < rank_list->rl_nr; i++) {

			rank = rank_list->rl_ranks[i];
			DBG_PRINT("Sending shutdown to rank=%d\n", rank);

			server_ep.ep_rank = rank;
			server_ep.ep_tag = 0;
			server_ep.ep_grp = grp;

			rc = crt_req_create(crt_ctx, &server_ep, RPC_SHUTDOWN, &rpc);
			if (rc != 0) {
				D_ERROR("crt_req_create() failed; rc=%d\n", rc);
				assert(0);
			}

			rc = crt_req_send(rpc, rpc_handle_reply, &sem);
			crtu_sem_timedwait(&sem, 10, __LINE__);
			DBG_PRINT("RPC response received from rank=%d\n", rank);
		}
	}

	D_FREE(rank_list->rl_ranks);
	D_FREE(rank_list);

	rc = crt_group_view_destroy(grp);
	if (rc != 0) {
		D_ERROR("crt_group_view_destroy() failed; rc=%d\n", rc);
		assert(0);
	}

	g_do_shutdown = true;
	pthread_join(progress_thread, NULL);

	sem_destroy(&sem);

	rc = crt_finalize();
	if (rc != 0) {
		D_ERROR("crt_finalize() failed with rc=%d\n", rc);
		assert(0);
	}

	DBG_PRINT("Client successfully finished\n");
	d_log_fini();

	return 0;
}
