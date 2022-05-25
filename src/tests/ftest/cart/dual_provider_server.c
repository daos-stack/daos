/*
 * (C) Copyright 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * Dual-provider server
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



static void
print_usage(const char *msg)
{
	printf("Error: %s\n", msg);
	printf("Usage: ./dual_provider_server -i 'iface0,iface1' ");
	printf("-d 'domain0,domain1' -p 'provider0,provider1' [-c 'num1,num2] ");
	printf("[-f 'file_to_transfer']\n");
	printf("\nLaunches server in dual provider mode based on provided args");
	printf("NOTE: Same argument values can be specified for both ");
	printf("servers, e.g. -i 'eth0,eth0'\n");
	printf("\nArguments:\n");
	printf("-i 'iface0,iface1'  : Specify two network interfaces to use; ");
	printf("e.g. 'eth0,eth1'\n");
	printf("-d 'domain0,domain1': Specify two domains to use; ");
	printf("e.g. 'eth0,eth1'\n");
	printf("-p 'provider0,provider1\n' : Specify providers to use; ");
	printf("e.g. 'ofi+tcp,ofi+verbs'\n");
	printf("-c 'num1,num2' : Specify number of contexts to allocate on each\n");
	printf("NOTE: first provider will be considered a primary one\n");
	printf("-f [filename]       : If set will transfer contents ");
	printf("of the specified file via bulk/rdma as part of 'PING' rpc\n");
}

void *
progress_fn(void *data)
{
	crt_context_t	*p_ctx = (crt_context_t *)data;
	int		rc;

	while (do_shutdown == 0)
		crt_progress(*p_ctx, 1000);

	sleep(1);
	rc = crt_context_destroy(*p_ctx, 1);
	if (rc != 0)
		D_ERROR("ctx destroy failed\n");
	pthread_exit(NULL);
}

static void
__split_arg(char *arg_to_split, char **first_arg, char **second_arg)
{
	char	*save_ptr;

	if (!arg_to_split)
		return;

	if (first_arg == NULL || second_arg == NULL) {
		print_usage("Fatal error, arguments NULL\n");
		exit(-1);
	}

	*first_arg = strtok_r(arg_to_split, ",", &save_ptr);
	if (*first_arg == NULL) {
		print_usage("Failed to parse first arg");
		exit(-1);
	}

	*second_arg = save_ptr;
	if (*second_arg == NULL) {
		print_usage("Failed to parse second arg");
		exit(-1);
	}
}


int main(int argc, char **argv)
{
	int		num_primary_ctx;
	int		num_secondary_ctx;
	crt_context_t	primary_ctx[NUM_PRIMARY_CTX_MAX];
	pthread_t	primary_progress_thread[NUM_PRIMARY_CTX_MAX];

	crt_context_t	secondary_ctx[NUM_SECONDARY_CTX_MAX];
	pthread_t	secondary_progress_thread[NUM_SECONDARY_CTX_MAX];

	int		rc;
	int		i;
	char		c;
	char		*arg_interface = NULL;
	char		*arg_domain = NULL;
	char		*arg_provider = NULL;
	char		*arg_mmap_file = NULL;
	char		*arg_num_ctx = NULL;
	char		default_iface0[] = "ib0";
	char		default_iface1[] = "ib1";
	char		default_domain0[] = "mlx5_0";
	char		default_domain1[] = "mlx5_1";
	char		default_provider0[] = "ofi+verbs;ofi_rxm\0";
	char		default_provider1[] = "ofi+tcp;ofi+rxm\0";

	char		*iface0, *iface1;
	char		*domain0, *domain1;
	char		*provider0, *provider1;
	char		*num_ctx0, *num_ctx1;
	char		*env_self_rank;
	char		*env_group_cfg;
	char		*my_uri;
	uint32_t	grp_size;
	crt_group_t	*grp;
	char		*uri;
	char		*saved_provider;
	char		*saved_iface;
	char		*saved_domain;


	/* Get self rank and a group config file from envs set by crt_launch */
	env_self_rank = getenv("CRT_L_RANK");
	env_group_cfg = getenv("CRT_L_GRP_CFG");

	if (env_self_rank == NULL || env_group_cfg == NULL) {
		printf("Error: This application is intended to be launched via crt_launch\n");
		return 0;
	}

	g_my_rank = atoi(env_self_rank);
	crtu_test_init(g_my_rank, 20, true, true);

	iface0 = default_iface0;
	iface1 = default_iface1;
	domain0 = default_domain0;
	domain1 = default_domain1;
	provider0 = default_provider0;
	provider1 = default_provider1;

	num_primary_ctx = NUM_PRIMARY_CTX_MAX;
	num_secondary_ctx = NUM_SECONDARY_CTX_MAX;

	while ((c = getopt(argc, argv, "i:p:d:f:c:")) != -1) {
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
		case 'f':
			arg_mmap_file = optarg;
			break;
		case 'c':
			arg_num_ctx = optarg;
			break;
		default:
			print_usage("invalid argument\n");
			return -1;
		}
	}

	saved_provider = strdup(arg_provider);
	saved_domain = strdup(arg_domain);
	saved_iface = strdup(arg_interface);

	__split_arg(arg_interface, &iface0, &iface1);
	__split_arg(arg_domain, &domain0, &domain1);
	__split_arg(arg_provider, &provider0, &provider1);

	if (arg_num_ctx) {
		__split_arg(arg_num_ctx, &num_ctx0, &num_ctx1);
		num_primary_ctx = atoi(num_ctx0);
		num_secondary_ctx = atoi(num_ctx1);
	}

	if (num_primary_ctx > NUM_PRIMARY_CTX_MAX) {
		printf("Error: Exceeded max alllowed %d for primary ctx\n",
		       NUM_PRIMARY_CTX_MAX);
		return -1;
	}

	if (num_secondary_ctx > NUM_SECONDARY_CTX_MAX) {
		printf("Error: Exceeded max alllowed %d for secondary ctx\n",
		       NUM_SECONDARY_CTX_MAX);
		return -1;
	}

	printf("----------------------------------------\n");
	printf("My_rank: %d\n", g_my_rank);
	printf("Provider0: '%s' Interface0: '%s' Domain0: '%s' #ctx: %d\n",
	       provider0, iface0, domain0, num_primary_ctx);
	printf("Provider1: '%s' Interface1: '%s' Domain1: '%s' #ctx: %d\n",
	       provider1, iface1, domain1, num_secondary_ctx);
	printf("File to transfer: '%s'\n",
	       arg_mmap_file ? arg_mmap_file : "none");
	printf("----------------------------------------\n\n");

	/* Done with parsing, now start the server up */
	rc = d_log_init();
	if (rc != 0) {
		D_ERROR("d_log_init() failed; rc=%d\n", rc);
		error_exit();
	}

	crt_init_options_t init_opts = {0};

	init_opts.cio_provider = saved_provider;
	init_opts.cio_interface = saved_iface;
	init_opts.cio_domain = saved_domain;

	rc = crt_init_opt(SERVER_GROUP_NAME,
			  CRT_FLAG_BIT_SERVER | CRT_FLAG_BIT_AUTO_SWIM_DISABLE,
			  &init_opts);
	if (rc != 0) {
		D_ERROR("crt_init() failed; rc=%d\n", rc);
		error_exit();
	}

	for (i = 0; i < num_primary_ctx; i++) {
		rc = crt_context_create(&primary_ctx[i]);
		if (rc != 0) {
			D_ERROR("Context %d creation failed; rc=%d\n", i, rc);
			error_exit();
		}

		rc = crt_context_uri_get(primary_ctx[i], &uri);
		if (rc != 0) {
			D_ERROR("crt_context_uri_get(%d) failed; rc=%d\n", i, rc);
			error_exit();
		}
		printf("Primary context[%d] uri=%s\n", i, uri);

		rc = pthread_create(&primary_progress_thread[i], 0, progress_fn, &primary_ctx[i]);
		if (rc != 0)
			error_exit();
	}

	for (i = 0;  i < num_secondary_ctx; i++) {
		rc = crt_context_create_secondary(&secondary_ctx[i], 0);
		if (rc != 0) {
			D_ERROR("Context %d creation failed; rc=%d\n", i, rc);
			error_exit();
		}

		rc = crt_context_uri_get(secondary_ctx[i], &uri);
		if (rc != 0) {
			D_ERROR("crt_context_uri_get(%d) failed; rc=%d\n", i, rc);
			error_exit();
		}
		printf("Secondary context[%d] uri=%s\n", i, uri);

		rc = pthread_create(&secondary_progress_thread[i], 0,
				    progress_fn, &secondary_ctx[i]);
		if (rc != 0) {
			error_exit();
		}
	}

	rc = crt_proto_register(&my_proto_fmt);
	if (rc != 0) {
		D_ERROR("crt_proto_register() failed; rc=%d\n", rc);
		error_exit();
	}

	grp = crt_group_lookup(NULL);
	if (!grp)
		error_exit();

	rc = crt_rank_self_set(g_my_rank);
	if (rc != 0)
		error_exit();

	{
		FILE	*f;
		int	parsed_rank;
		char	parsed_addr[256];

		f = fopen(env_group_cfg, "r");
		if (!f) {
			D_ERROR("Failed to open %s\n", env_group_cfg);
			error_exit();
		}

		while (1) {
			rc = fscanf(f, "%8d %254s", &parsed_rank, parsed_addr);
			if (rc == EOF) {
				rc = 0;
				break;
			}

			if (parsed_rank == g_my_rank)
				continue;


			DBG_PRINT("Rank=%d uri='%s'\n", parsed_rank, parsed_addr);
			rc = crt_group_primary_rank_add(primary_ctx[0], grp,
							parsed_rank, parsed_addr);

			if (rc != 0) {
				D_ERROR("Failed to add %d %s; rc=%d\n",
					parsed_rank, parsed_addr, rc);
				break;
			}
		}
	}

	rc = crt_rank_uri_get(grp, g_my_rank, 0, &my_uri);
	if (rc)
		error_exit();


	rc = crt_group_size(NULL, &grp_size);
	if (rc)
		error_exit();

	DBG_PRINT("self_rank=%d uri=%s file=%s group_size=%d\n",
		  g_my_rank, my_uri, env_group_cfg, grp_size);

	D_FREE(my_uri);


	if (g_my_rank == 0) {
		DBG_PRINT("Saving group config info\n");
		rc = crt_group_config_save(NULL, true);
		if (rc)
			error_exit();
	}

	{
		FILE	*f;
		char	*filename;
		char	*pri_uri0;
		char	*sec_uri0;

		D_ASPRINTF(filename, "/tmp/%s_rank_%d_uris.cart", SERVER_GROUP_NAME, g_my_rank);
		if (filename == NULL)
			error_exit();

		f = fopen(filename, "w");
		if (f == NULL)
			error_exit();


		rc = crt_context_uri_get(primary_ctx[0], &pri_uri0);
		if (rc)
			error_exit();

		rc = crt_context_uri_get(secondary_ctx[0], &sec_uri0);
		if (rc)
			error_exit();

		fprintf(f, "%s\n", pri_uri0);
		fprintf(f, "%s\n", sec_uri0);

		fclose(f);
		D_FREE(filename);
	}

	for (i = 0; i < num_primary_ctx; i++)
		pthread_join(primary_progress_thread[i], NULL);

	for (i = 0; i < num_secondary_ctx; i++)
		pthread_join(secondary_progress_thread[i], NULL);


	rc = crt_finalize();
	if (rc != 0)
		error_exit();

	d_log_fini();

	free(saved_provider);
	free(saved_domain);
	free(saved_iface);
	return 0;
}

