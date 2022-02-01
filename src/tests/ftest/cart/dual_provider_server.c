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
	printf("-d 'domain0,domain1' -p 'provider0,provider1' [-f 'file_to_transfer']\n");
	printf("\nLaunches 2 servers based on provided args");
	printf("NOTE: Same argument values can be specified for both ");
	printf("servers, e.g. -i 'eth0,eth0'\n");
	printf("\nArguments:\n");
	printf("-i 'iface0,iface1'  : Specify two network interfaces to use; ");
	printf("e.g. 'eth0,eth1'\n");
	printf("-d 'domain0,domain1': Specify two domains to use; ");
	printf("e.g. 'eth0,eth1'\n");
	printf("-p 'provider0,provider1\n' : Specify providers to use; ");
	printf("e.g. 'ofi+tcp,ofi+verbs'\n");
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

int main(int argc, char **argv)
{
	crt_context_t	primary_ctx[NUM_PRIMARY_CTX];
	pthread_t	primary_progress_thread[NUM_PRIMARY_CTX];

//	crt_context_t	secondary_ctx[NUM_SECONDARY_CTX];
//	pthread_t	secondary_thread[NUM_SECONDARY_CTX];

	int		rc;
	int		i;
	char		c;
	char		*arg_interface = NULL;
	char		*arg_domain = NULL;
	char		*arg_provider = NULL;
	char		*arg_mmap_file = NULL;
	char		default_iface0[] = "ib0";
	char		default_iface1[] = "ib1";
	char		default_domain0[] = "mlx5_0";
	char		default_domain1[] = "mlx5_1";
	char		default_provider0[] = "ofi+verbs;ofi_rxm\0";
	char		default_provider1[] = "ofi+tcp;ofi+rxm\0";

	char		*iface0, *iface1;
	char		*domain0, *domain1;
	char		*provider0, *provider1;
	char		*env_self_rank;
	char		*env_group_cfg;
	char		*my_uri;
	uint32_t	grp_size;
	crt_group_t	*grp;
	char		*save_ptr;

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

	while ((c = getopt(argc, argv, "i:p:d:f:")) != -1) {
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
		default:
			print_usage("invalid argument\n");
			return -1;
		}
	}
	
	if (arg_interface) {
		iface0 = strtok_r(arg_interface, ",", &save_ptr);
		if (iface0 == NULL) {
			print_usage("Failed to parse iface0");
			return -1;
		}

		iface1 = save_ptr;
		if (iface1 == NULL) {
			print_usage("Failed to parse iface1");
			return -1;
		}
	}

	if (arg_domain) {
		domain0 = strtok_r(arg_domain, ",", &save_ptr);
		if (domain0 == NULL) {
			print_usage("Failed to parse domain0");
			return -1;
		}

		domain1 = save_ptr;
		if (domain1 == NULL) {
			print_usage("Failed to parse domain1");
			return -1;
		}
	}

	if (arg_provider) {
		provider0 = strtok_r(arg_provider, ",", &save_ptr);
		if (provider0 == NULL) {
			print_usage("Failed to parse domain0");
			return -1;
		}

		provider1 = save_ptr;
		if (provider1 == NULL) {
			print_usage("Failed to parse domain1");
			return -1;
		}
	}

	printf("----------------------------------------\n");
	printf("My_rank: %d\n", g_my_rank);
	printf("Provider0: '%s' Interface0: '%s' Domain0: '%s'\n", provider0, iface0, domain0);
	printf("Provider1: '%s' Interface1: '%s' Domain1: '%s'\n", provider1, iface1, domain1);
	printf("File to transfer: '%s'\n",
	       arg_mmap_file ? arg_mmap_file : "none");
	printf("----------------------------------------\n\n");

	rc = d_log_init();
	if (rc != 0) {
		D_ERROR("d_log_init() failed; rc=%d\n", rc);
		error_exit();
	}

	rc = crt_init(SERVER_GROUP_NAME,
			CRT_FLAG_BIT_SERVER | CRT_FLAG_BIT_AUTO_SWIM_DISABLE);
	if (rc != 0) {
		D_ERROR("crt_init() failed; rc=%d\n", rc);
		error_exit();
	}

	for (i = 0; i < NUM_PRIMARY_CTX; i++) {
		rc = crt_context_create(&primary_ctx[i]);
		if (rc != 0) {
			D_ERROR("Context %d creation failed; rc=%d\n", i, rc);
			error_exit();
		}

		rc = pthread_create(&primary_progress_thread[i], 0, progress_fn, &primary_ctx[i]);
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

	DBG_PRINT("self_rank=%d uri=%s file=%s group_size=%d\n", g_my_rank, my_uri, env_group_cfg, grp_size);

	D_FREE(my_uri);


	if (g_my_rank == 0) {
		DBG_PRINT("Saving group config info\n");
		rc = crt_group_config_save(NULL, true);
		if (rc)
			error_exit();
	}

	for (i = 0; i < NUM_PRIMARY_CTX; i++)
		pthread_join(primary_progress_thread[i], NULL);

	rc = crt_finalize();
	if (rc != 0)
		error_exit();

	d_log_fini();

	return 0;
}

