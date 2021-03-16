/*
 * (C) Copyright 2018-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * Server utilizing dual interfaces to check connectivity
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <semaphore.h>
#include <cart/api.h>
#include <cart/types.h>

#include "tests_common.h"

#define MY_BASE 0x010000000
#define MY_VER  0

#define NUM_SERVER_CTX 8

#define RPC_DECLARE(name)						\
	CRT_RPC_DECLARE(name, CRT_ISEQ_##name, CRT_OSEQ_##name)		\
	CRT_RPC_DEFINE(name, CRT_ISEQ_##name, CRT_OSEQ_##name)

enum {
	RPC_PING = CRT_PROTO_OPC(MY_BASE, MY_VER, 0),
	RPC_SHUTDOWN
} rpc_id_t;

#define CRT_ISEQ_RPC_PING	/* input fields */		 \
	((uint64_t)		(src_rank)		CRT_VAR) \
	((uint64_t)		(dst_tag)		CRT_VAR)

#define CRT_OSEQ_RPC_PING	/* output fields */		 \
	((uint64_t)		(rc)			CRT_VAR)

#define CRT_ISEQ_RPC_SHUTDOWN	/* input fields */		 \
	((uint64_t)		(field)			CRT_VAR)

#define CRT_OSEQ_RPC_SHUTDOWN	/* output fields */		 \
	((uint64_t)		(field)			CRT_VAR)

int handler_ping(crt_rpc_t *rpc);
int handler_shutdown(crt_rpc_t *rpc);

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

int handler_ping(crt_rpc_t *rpc)
{
	struct RPC_PING_in	*input;
	struct RPC_PING_out	*output;
	int			my_tag;
	d_rank_t		hdr_dst_rank;
	uint32_t		hdr_dst_tag;
	d_rank_t		hdr_src_rank;
	int			rc = 0;

	input = crt_req_get(rpc);
	output = crt_reply_get(rpc);

//	DBG_PRINT("Ping handler invoked\n");
	rc = crt_req_src_rank_get(rpc, &hdr_src_rank);
	D_ASSERTF(rc == 0, "crt_req_src_rank_get() failed; rc=%d\n", rc);

	rc = crt_req_dst_rank_get(rpc, &hdr_dst_rank);
	D_ASSERTF(rc == 0, "crt_req_dst_rank_get() failed; rc=%d\n", rc);

	rc = crt_req_dst_tag_get(rpc, &hdr_dst_tag);
	D_ASSERTF(rc == 0, "crt_req_dst_tag_get() failed; rc=%d\n", rc);

	crt_context_idx(rpc->cr_ctx, &my_tag);

	if (my_tag != input->dst_tag || my_tag != hdr_dst_tag) {
		D_ERROR("Incorrect tag Expected %lu got %d (hdr=%d)\n",
			input->dst_tag, my_tag, hdr_dst_tag);
		assert(0);
	}
	if (hdr_src_rank != input->src_rank) {
		D_ERROR("Expected %lu got %d\n", input->src_rank, hdr_src_rank);
		rc = -DER_INVAL;
//		assert(0);
	}

	output->rc = rc;
	crt_reply_send(rpc);

	return 0;
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

int handler_shutdown(crt_rpc_t *rpc)
{
	crt_reply_send(rpc);
	tc_progress_stop();
	return 0;
}

#define MAX_URI 128

static int
server_main(d_rank_t my_rank, const char *str_port, const char *str_interface,
	    const char *str_domain, const char *str_provider,
	    int fd_read, int fd_write)
{
	int			i;
	char			*my_uri;
	crt_group_t		*grp;
	crt_context_t		crt_ctx[NUM_SERVER_CTX];
	pthread_t		progress_thread[NUM_SERVER_CTX];
	struct RPC_PING_in	*input;
	int			rc;
	crt_endpoint_t		server_ep;
	sem_t			sem;
	crt_rpc_t		*rpc = NULL;
	char			other_server_uri[MAX_URI];
	int			tag = 0;

	setenv("OFI_PORT", str_port, 1);
	setenv("OFI_INTERFACE", str_interface, 1);
	setenv("OFI_DOMAIN", str_domain, 1);
	setenv("CRT_PHY_ADDR_STR", str_provider, 1);
	setenv("FI_UNIVERSE_SIZE", "1024", 1);
	setenv("D_LOG_MASK", "ERR", 1);

	/* rank, num_attach_retries, is_server, assert_on_error */
	tc_test_init(my_rank, 20, true, true);

	rc = d_log_init();
	assert(rc == 0);

	DBG_PRINT("Starting server rank=%d\n", my_rank);

	rc = sem_init(&sem, 0, 0);
	if (rc != 0) {
		D_ERROR("sem_init() failed; rc=%d\n", rc);
		assert(0);
	}

	rc = crt_init("server_grp", CRT_FLAG_BIT_SERVER |
		      CRT_FLAG_BIT_AUTO_SWIM_DISABLE);
	if (rc != 0) {
		D_ERROR("crt_init() failed; rc=%d\n", rc);
		assert(0);
	}

	grp = crt_group_lookup(NULL);
	if (!grp) {
		D_ERROR("Failed to lookup group\n");
		assert(0);
	}

	rc = crt_rank_self_set(my_rank);
	if (rc != 0) {
		D_ERROR("crt_rank_self_set(%d) failed; rc=%d\n",
			my_rank, rc);
		assert(0);
	}

	for (i = 0; i < NUM_SERVER_CTX; i++) {
		rc = crt_context_create(&crt_ctx[i]);
		if (rc != 0) {
			D_ERROR("crt_context_create() ctx=%d failed; rc=%d\n",
				i, rc);
			assert(0);
		}

		rc = pthread_create(&progress_thread[i], 0,
				    tc_progress_fn, &crt_ctx[i]);
		if (rc != 0) {
			D_ERROR("pthread_create() ctx=%d failed; rc=%d\n",
				i, rc);
			assert(0);
		}
	}

	rc = crt_rank_uri_get(grp, my_rank, 0, &my_uri);
	if (rc != 0) {
		D_ERROR("crt_rank_uri_get() failed; rc=%d\n", rc);
		assert(0);
	}

	rc = crt_proto_register(&my_proto_fmt);
	if (rc != 0) {
		D_ERROR("crt_proto_register() failed; rc=%d\n", rc);
		assert(0);
	}

	DBG_PRINT("my_rank=%d uri=%s\n", my_rank, my_uri);

	rc = write(fd_write, my_uri, strlen(my_uri) + 1);
	if (rc <= 0) {
		D_ERROR("Failed to write uri to a file\n");
		assert(0);
	}
	D_FREE(my_uri);

	syncfs(fd_write);

	sleep(1);
	memset(other_server_uri, 0x0, MAX_URI);

	lseek(fd_read, 0, SEEK_SET);
	rc = read(fd_read, other_server_uri, MAX_URI);
	if (rc < 0) {
		perror("Failed to read ");
		D_ERROR("Failed ot read uri from a file\n");
		assert(0);
	}

	DBG_PRINT("Other servers uri is '%s'\n", other_server_uri); 	

	d_rank_t other_rank;
	if (my_rank == 0)
		other_rank = 1;
	else
		other_rank = 0;

	rc = crt_group_primary_rank_add(crt_ctx[0], grp, other_rank,
			other_server_uri);
	if (rc != 0) {
		D_ERROR("Failed to add rank=%d uri='%s'\n",
			other_rank, other_server_uri);
		assert(0);
	}

	server_ep.ep_rank = other_rank;
	server_ep.ep_tag = tag;
	server_ep.ep_grp = grp;

	rc = crt_req_create(crt_ctx[0], &server_ep, RPC_PING, &rpc);
	if (rc != 0) {
		D_ERROR("crt_req_create() failed; rc=%d\n", rc);
		assert(0);
	}

	input = crt_req_get(rpc);
	input->src_rank = my_rank;
	input->dst_tag = tag;

	rc = crt_req_send(rpc, rpc_handle_reply, &sem);
	if (rc != 0) {
		D_ERROR("Failed to send rpc; rc=%d\n", rc);
		assert(0);
	}

	tc_sem_timedwait(&sem, 10, __LINE__);
	DBG_PRINT("Ping successful to rank=%d tag=%d\n", other_rank, tag);

	/* Shutdown */
	server_ep.ep_rank = other_rank;
	server_ep.ep_tag = tag;
	server_ep.ep_grp = grp;

	rc = crt_req_create(crt_ctx[0], &server_ep, RPC_SHUTDOWN, &rpc);
	if (rc != 0) {
		D_ERROR("crt_req_create() failed; rc=%d\n", rc);
		assert(0);
	}

	input = crt_req_get(rpc);
	input->src_rank = my_rank;
	input->dst_tag = tag;

	rc = crt_req_send(rpc, rpc_handle_reply, &sem);
	if (rc != 0) {
		D_ERROR("Failed to send rpc; rc=%d\n", rc);
		assert(0);
	}

	tc_sem_timedwait(&sem, 10, __LINE__);

	/* Wait until shutdown is issued and progress threads exit */
	for (i = 0; i < NUM_SERVER_CTX; i++)
		pthread_join(progress_thread[i], NULL);

	rc = crt_finalize();
	if (rc != 0) {
		D_ERROR("crt_finalize() failed with rc=%d\n", rc);
		assert(0);
	}

	d_log_fini();

	return rc;
}

int main(int argc, char **argv)
{
	int	pid;
	int	fd0, fd1;
	char	tmp_file0[] = "/tmp/server0-XXXXXX";
	char	tmp_file1[] = "/tmp/server1-XXXXXX";
	int	c;
	char	*provider;
	char	*arg_interface = NULL;
	char	*arg_domain = NULL;
	char	*arg_provider = NULL;
	char	*iface0, *iface1;
	char	*save_ptr;
	char	*domain0, *domain1;
	char	default_iface0[] = "ib0";
	char	default_iface1[] = "ib1";
	char	default_domain0[] = "mlx5_0";
	char	default_domain1[] = "mlx5_1";
	char	default_provider[] = "ofi+verbs;ofi_rxm\0";

	while ((c = getopt(argc, argv, "i:p:d:")) != -1) {
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
		}
	}

	iface0 = default_iface0;
	iface1 = default_iface1;
	domain0 = default_domain0;
	domain1 = default_domain1;
	provider = default_provider;

	if (arg_interface) {
		iface0 = strtok_r(arg_interface, ",", &save_ptr);
		iface1 = strtok_r(NULL, ",", &save_ptr);
	}

	if (arg_domain) {
		domain0 = strtok_r(arg_domain, ",", &save_ptr);
		domain1 = strtok_r(NULL, ",", &save_ptr);
	}

	if (arg_provider)
		provider = arg_provider;

	
	printf("----------------------------------------\n");
	printf("Provider: '%s'\n", provider);
	printf("Interface0: '%s' Domain0: '%s'\n", iface0, domain0);
	printf("Interface1: '%s' Domain1: '%s'\n", iface1, domain1);
	printf("----------------------------------------\n\n");

	fd0 = mkstemp(tmp_file0);
	fd1 = mkstemp(tmp_file1);

	pid = fork();
	if (pid == 0) {
		server_main(0, "31337", iface0, domain0, provider, fd0, fd1);
	} else {
		server_main(1, "32337", iface1, domain1,  provider, fd1, fd0);
	}

	if (pid) {
		close(fd0);
		close(fd1);
	}

	return 0;
}

