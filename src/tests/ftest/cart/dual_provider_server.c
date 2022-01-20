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

#define MY_BASE 0x010000000
#define MY_VER  0

#define NUM_PRIMARY_CTX 8
#define NUM_SECONDARY_CTX 8

#define RPC_DECLARE(name)					\
	CRT_RPC_DECLARE(name, CRT_ISEQ_##name, CRT_OSEQ_##name)	\
	CRT_RPC_DEFINE(name, CRT_ISEQ_##name, CRT_OSEQ_##name)

enum {
	RPC_PING = CRT_PROTO_OPC(MY_BASE, MY_VER, 0),
	RPC_SHUTDOWN
} rpc_id_t;

#define CRT_ISEQ_RPC_PING	/* input fields */		 \
	((crt_bulk_t)		(bulk_hdl1)		CRT_VAR) \
	((crt_bulk_t)		(bulk_hdl2)		CRT_VAR) \
	((uint64_t)		(size1)			CRT_VAR) \
	((uint64_t)		(size2)			CRT_VAR)

#define CRT_OSEQ_RPC_PING	/* output fields */		 \
	((crt_bulk_t)		(ret_bulk)		CRT_VAR) \
	((int64_t)		(rc)			CRT_VAR)

#define CRT_ISEQ_RPC_SHUTDOWN	/* input fields */		 \
	((uint64_t)		(field)			CRT_VAR)

#define CRT_OSEQ_RPC_SHUTDOWN	/* output fields */		 \
	((uint64_t)		(field)			CRT_VAR)

static int handler_ping(crt_rpc_t *rpc);
static int handler_shutdown(crt_rpc_t *rpc);

RPC_DECLARE(RPC_PING);
RPC_DECLARE(RPC_SHUTDOWN);

struct crt_proto_rpc_format my_proto_rpc_fmt[] = {
	{
		.prf_flags	= 0,
		.prf_req_fmt	= &CQF_RPC_PING,
		.prf_hdlr	= (void *)handler_ping,
		.prf_co_ops	= NULL,
	}, {
		.prf_flags	= 0,
		.prf_req_fmt	= &CQF_RPC_SHUTDOWN,
		.prf_hdlr	= (void *)handler_shutdown,
		.prf_co_ops	= NULL,
	}
};

struct crt_proto_format my_proto_fmt = {
	.cpf_name = "my-proto",
	.cpf_ver = MY_VER,
	.cpf_count = ARRAY_SIZE(my_proto_rpc_fmt),
	.cpf_prf = &my_proto_rpc_fmt[0],
	.cpf_base = MY_BASE,
};


static int
handler_ping(crt_rpc_t *rpc)
{
	struct RPC_PING_in	*input;
	struct RPC_PING_out	*output;
	int			rc = 0;

	input = crt_req_get(rpc);
	output = crt_reply_get(rpc);

	crt_reply_send(rpc);
	return 0;
}

static int
handler_shutdown(crt_rpc_t *rpc)
{
	crt_reply_send(rpc);
	exit(0); // TODO: Fix
	return 0;
}

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
	printf("e.g. 'ofi+tcp,ofi+verbs'\n")
	printf("NOTE: first provider will be considered a primary one\n");
	printf("-f [filename]       : If set will transfer contents ");
	printf("of the specified file via bulk/rdma as part of 'PING' rpc\n");
}

int main(int argc, char **argv)
{
	crt_context_t	primary_ctx[NUM_PRIMARY_CTX];
	pthread_t	progress_thread[NUM_PRIMARY_CTX];

	crt_context_t	secondary_ctx[NUM_SECONDARY_CTX];
	pthread_t	secondary_thread[NUM_SECONDARY_CTX];

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
	printf("Provider0: '%s' Interface0: '%s' Domain0: '%s'\n", provider0, iface0, domain0);
	printf("Provider1: '%s' Interface1: '%s' Domain1: '%s'\n", provider1, iface1, domain1);
	printf("File to transfer: '%s'\n",
	       arg_mmap_file ? arg_mmap_file : "none");
	printf("----------------------------------------\n\n");

	return 0;
}

