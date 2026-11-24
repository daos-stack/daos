/*
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * Basic multiprovider connectivity test. Two server ranks each progress a primary
 * and a secondary context. They exchange primary URIs through files, then rank 0
 * requests rank 1's secondary URI over the primary provider and publishes attach
 * information for both fabrics. Separate clients ping both ranks over each
 * provider and verify the receiving context. The secondary client also sends a
 * bound bulk to rank 0, which forwards it to rank 1 over the primary provider.
 * Rank 1 pulls and verifies the data over the secondary provider before replying.
 * A primary client then stops the servers.
 */

#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <cart/api.h>
#include "crt_utils.h"

#define GROUP_NAME  "multiprovider_test"
#define PROTO_BASE  0x010000000
#define URI_SIZE    256
#define BULK_SIZE   (64U * 1024U)
#define BULK_MARKER 0x12345678U

enum {
	OP_GET_URI = CRT_PROTO_OPC(PROTO_BASE, 0, 0),
	OP_PING,
	OP_SHUTDOWN,
	OP_FORWARD_BULK,
	OP_PULL_BULK,
};

#define CRT_ISEQ_GET_URI  ((uint32_t)(unused)CRT_VAR)
#define CRT_OSEQ_GET_URI  ((d_string_t)(uri)CRT_VAR)
#define CRT_ISEQ_PING     ((uint32_t)(expected)CRT_VAR)
#define CRT_OSEQ_PING     ((uint32_t)(provider)CRT_VAR)((uint32_t)(rank)CRT_VAR)
#define CRT_ISEQ_SHUTDOWN ((uint32_t)(unused)CRT_VAR)
#define CRT_OSEQ_SHUTDOWN ((uint32_t)(unused)CRT_VAR)
#define CRT_ISEQ_BULK                                                                              \
	((crt_bulk_t)(bulk)CRT_VAR)((crt_bulk_t)(null_bulk)CRT_VAR)((uint32_t)(marker)CRT_VAR)
#define CRT_OSEQ_BULK ((uint32_t)(verified)CRT_VAR)((uint32_t)(marker)CRT_VAR)

CRT_RPC_DECLARE(GET_URI, CRT_ISEQ_GET_URI, CRT_OSEQ_GET_URI)
CRT_RPC_DEFINE(GET_URI, CRT_ISEQ_GET_URI, CRT_OSEQ_GET_URI)
CRT_RPC_DECLARE(PING, CRT_ISEQ_PING, CRT_OSEQ_PING)
CRT_RPC_DEFINE(PING, CRT_ISEQ_PING, CRT_OSEQ_PING)
CRT_RPC_DECLARE(SHUTDOWN, CRT_ISEQ_SHUTDOWN, CRT_OSEQ_SHUTDOWN)
CRT_RPC_DEFINE(SHUTDOWN, CRT_ISEQ_SHUTDOWN, CRT_OSEQ_SHUTDOWN)
CRT_RPC_DECLARE(BULK, CRT_ISEQ_BULK, CRT_OSEQ_BULK)
CRT_RPC_DEFINE(BULK, CRT_ISEQ_BULK, CRT_OSEQ_BULK)

static crt_context_t contexts[2];
static char         *secondary_uri;
static d_rank_t      self_rank;

static void
get_uri_handler(crt_rpc_t *rpc)
{
	struct GET_URI_out *out = crt_reply_get(rpc);

	/* Discover secondary addresses using the already connected primary fabric. */
	out->uri = secondary_uri;
	D_ASSERT(crt_reply_send(rpc) == 0);
}

static void
ping_handler(crt_rpc_t *rpc)
{
	struct PING_in  *in  = crt_req_get(rpc);
	struct PING_out *out = crt_reply_get(rpc);

	/* Verify the receiving context rather than echoing the client's provider choice. */
	out->provider = crt_context_is_primary(rpc->cr_ctx) ? 0 : 1;
	out->rank     = self_rank;
	D_ASSERTF(out->provider == in->expected, "Wrong provider: expected %u, got %u\n",
		  in->expected, out->provider);
	D_ASSERT(crt_reply_send(rpc) == 0);
}

static void
shutdown_handler(crt_rpc_t *rpc)
{
	D_ASSERT(crt_reply_send(rpc) == 0);
	crtu_progress_stop();
}

static unsigned char
bulk_pattern(uint32_t offset)
{
	return (unsigned char)((offset * 17U + 23U) & 0xffU);
}

static void
forward_bulk_complete(const struct crt_cb_info *info)
{
	crt_rpc_t       *request = info->cci_arg;
	struct BULK_out *out     = crt_reply_get(request);
	struct BULK_out *reply   = crt_reply_get(info->cci_rpc);

	D_ASSERTF(info->cci_rc == 0, "Forwarded bulk RPC failed: %d\n", info->cci_rc);

	*out = *reply;
	D_ASSERT(crt_reply_send(request) == 0);

	crt_req_decref(request);
}

static void
forward_bulk_handler(crt_rpc_t *rpc)
{
	struct BULK_in *in = crt_req_get(rpc);
	struct BULK_in *forward;
	crt_endpoint_t  ep = {.ep_grp = crt_group_lookup(NULL), .ep_rank = 1, .ep_tag = 0};
	crt_rpc_t      *request;
	uint32_t        index;

	D_ASSERT(self_rank == 0 && !crt_context_is_primary(rpc->cr_ctx));
	D_ASSERT(in->null_bulk == CRT_BULK_NULL && in->marker == BULK_MARKER);
	D_ASSERT(crt_bulk_get_provider_index(in->bulk, &index) == 0 && index == 1);

	DBG_PRINT("Rank 0 forwarding secondary bound bulk to rank 1 over primary provider\n");
	D_ASSERT(crt_req_create(contexts[0], &ep, OP_PULL_BULK, &request) == 0);

	forward  = crt_req_get(request);
	*forward = *in;

	/* Outgoing inputs are borrowed; retain the incoming RPC and its decoded bulk. */
	crt_req_addref(rpc);
	D_ASSERT(crt_req_set_timeout(request, 10) == 0);
	D_ASSERT(crt_req_send(request, forward_bulk_complete, rpc) == 0);
}

struct bulk_transfer {
	crt_rpc_t    *request;
	crt_bulk_t    local;
	unsigned char buffer[BULK_SIZE];
};

static int
pull_bulk_complete(const struct crt_bulk_cb_info *info)
{
	struct bulk_transfer *transfer = info->bci_arg;
	struct BULK_out      *out      = crt_reply_get(transfer->request);
	uint32_t              i;

	D_ASSERTF(info->bci_rc == 0, "Secondary bulk transfer failed: %d\n", info->bci_rc);

	for (i = 0; i < BULK_SIZE; i++)
		D_ASSERTF(transfer->buffer[i] == bulk_pattern(i),
			  "Bulk data mismatch at offset %u: expected %u, got %u\n", i,
			  bulk_pattern(i), transfer->buffer[i]);

	out->verified = BULK_SIZE;
	out->marker   = BULK_MARKER;
	DBG_PRINT("Rank 1 verified %u forwarded bulk bytes over secondary provider\n", BULK_SIZE);

	D_ASSERT(crt_bulk_free(transfer->local) == 0);
	D_ASSERT(crt_reply_send(transfer->request) == 0);

	crt_req_decref(transfer->request);
	D_FREE(transfer);

	return 0;
}

static void
pull_bulk_handler(crt_rpc_t *rpc)
{
	struct BULK_in       *in = crt_req_get(rpc);
	struct bulk_transfer *transfer;
	struct crt_bulk_desc  desc = {0};
	d_iov_t               iov;
	d_sg_list_t           sgl = {.sg_nr = 1, .sg_nr_out = 1, .sg_iovs = &iov};
	uint32_t              index;

	D_ASSERT(self_rank == 1 && crt_context_is_primary(rpc->cr_ctx));
	D_ASSERT(in->null_bulk == CRT_BULK_NULL && in->marker == BULK_MARKER);
	D_ASSERT(crt_bulk_get_provider_index(in->bulk, &index) == 0 && index == 1);

	D_ALLOC_PTR(transfer);
	D_ASSERT(transfer != NULL);

	transfer->request = rpc;
	d_iov_set(&iov, transfer->buffer, sizeof(transfer->buffer));

	/* Register the destination with the bulk provider, not the incoming RPC provider. */
	D_ASSERT(crt_bulk_create(contexts[1], &sgl, CRT_BULK_RW, &transfer->local) == 0);

	desc.bd_rpc        = rpc;
	desc.bd_bulk_op    = CRT_BULK_GET;
	desc.bd_remote_hdl = in->bulk;
	desc.bd_local_hdl  = transfer->local;
	desc.bd_len        = BULK_SIZE;

	crt_req_addref(rpc);
	DBG_PRINT("Rank 1 received primary RPC; pulling bound bulk over secondary provider\n");
	D_ASSERT(crt_bulk_bind_transfer_ctx(contexts[1], &desc, pull_bulk_complete, transfer,
					    NULL) == 0);
}

static struct crt_proto_rpc_format formats[] = {
    {.prf_req_fmt = &CQF_GET_URI, .prf_hdlr = get_uri_handler},
    {.prf_req_fmt = &CQF_PING, .prf_hdlr = ping_handler},
    {.prf_req_fmt = &CQF_SHUTDOWN, .prf_hdlr = shutdown_handler},
    {.prf_req_fmt = &CQF_BULK, .prf_hdlr = forward_bulk_handler},
    {.prf_req_fmt = &CQF_BULK, .prf_hdlr = pull_bulk_handler},
};

static struct crt_proto_format protocol = {
    .cpf_name  = "multiprovider",
    .cpf_ver   = 0,
    .cpf_count = ARRAY_SIZE(formats),
    .cpf_prf   = formats,
    .cpf_base  = PROTO_BASE,
};

struct rpc_result {
	sem_t    sem;
	int      rc;
	uint32_t provider;
	uint32_t rank;
	uint32_t verified;
	uint32_t marker;
	char     uri[URI_SIZE];
};

static void
rpc_complete(const struct crt_cb_info *info)
{
	struct rpc_result *result = info->cci_arg;

	result->rc = info->cci_rc;

	if (result->rc == 0 && info->cci_rpc->cr_opc == OP_GET_URI) {
		struct GET_URI_out *out = crt_reply_get(info->cci_rpc);

		/* Reply storage is released after this callback returns. */
		if (out->uri == NULL || strlen(out->uri) >= sizeof(result->uri))
			result->rc = -DER_INVAL;
		else
			strcpy(result->uri, out->uri);
	} else if (result->rc == 0 && info->cci_rpc->cr_opc == OP_PING) {
		struct PING_out *out = crt_reply_get(info->cci_rpc);

		result->provider = out->provider;
		result->rank     = out->rank;
	} else if (result->rc == 0 && info->cci_rpc->cr_opc == OP_FORWARD_BULK) {
		struct BULK_out *out = crt_reply_get(info->cci_rpc);

		result->verified = out->verified;
		result->marker   = out->marker;
	}

	sem_post(&result->sem);
}

static void
send_forwarded_bulk(crt_context_t ctx, crt_group_t *group)
{
	unsigned char    *buffer;
	struct rpc_result result = {0};
	crt_endpoint_t    ep     = {.ep_grp = group, .ep_rank = 0, .ep_tag = 0};
	crt_bulk_t        bulk;
	crt_rpc_t        *rpc;
	struct BULK_in   *in;
	d_iov_t           iov;
	d_sg_list_t       sgl = {.sg_nr = 1, .sg_nr_out = 1, .sg_iovs = &iov};
	uint32_t          i;

	D_ALLOC(buffer, BULK_SIZE);
	D_ASSERT(buffer != NULL);

	for (i = 0; i < BULK_SIZE; i++)
		buffer[i] = bulk_pattern(i);

	d_iov_set(&iov, buffer, BULK_SIZE);
	D_ASSERT(crt_bulk_create(ctx, &sgl, CRT_BULK_RO, &bulk) == 0);
	D_ASSERT(crt_bulk_bind(bulk, ctx) == 0);

	D_ASSERT(crt_req_create(ctx, &ep, OP_FORWARD_BULK, &rpc) == 0);

	in            = crt_req_get(rpc);
	in->bulk      = bulk;
	in->null_bulk = CRT_BULK_NULL;
	in->marker    = BULK_MARKER;

	D_ASSERT(sem_init(&result.sem, 0, 0) == 0);
	D_ASSERT(crt_req_set_timeout(rpc, 15) == 0);
	DBG_PRINT("Sending %u bound bulk bytes over secondary provider for primary forwarding\n",
		  BULK_SIZE);
	D_ASSERT(crt_req_send(rpc, rpc_complete, &result) == 0);

	D_ASSERT(crtu_sem_timedwait(&result.sem, 20, __LINE__) == 0);
	D_ASSERTF(result.rc == 0, "Forwarded bulk test failed: %d\n", result.rc);
	D_ASSERT(result.verified == BULK_SIZE && result.marker == BULK_MARKER);
	DBG_PRINT("Forwarded bulk passed: %u bytes verified\n", result.verified);

	D_ASSERT(sem_destroy(&result.sem) == 0);
	D_ASSERT(crt_bulk_free(bulk) == 0);
	D_FREE(buffer);
}

static int
send_rpc(crt_context_t ctx, crt_group_t *group, d_rank_t rank, crt_opcode_t opcode,
	 uint32_t expected, struct rpc_result *result)
{
	crt_endpoint_t ep = {.ep_grp = group, .ep_rank = rank, .ep_tag = 0};
	crt_rpc_t     *rpc;
	int            rc;

	memset(result, 0, sizeof(*result));
	D_ASSERT(sem_init(&result->sem, 0, 0) == 0);

	rc = crt_req_create(ctx, &ep, opcode, &rpc);
	if (rc != 0)
		goto out;

	if (opcode == OP_PING) {
		struct PING_in *in = crt_req_get(rpc);

		in->expected = expected;
	}

	D_ASSERT(crt_req_set_timeout(rpc, 3) == 0);
	rc = crt_req_send(rpc, rpc_complete, result);
	if (rc != 0)
		goto out;

	if (crtu_sem_timedwait(&result->sem, 10, __LINE__) != 0) {
		fprintf(stderr, "RPC to rank %u did not complete\n", rank);
		exit(EXIT_FAILURE);
	}
	rc = result->rc;
out:
	sem_destroy(&result->sem);
	return rc;
}

static char *
uri_filename(const char *directory, d_rank_t rank)
{
	char *path;

	D_ASPRINTF(path, "%s/rank-%u.uri", directory, rank);
	D_ASSERT(path != NULL);
	return path;
}

static void
publish_uri(const char *directory, d_rank_t rank, const char *uri)
{
	char *path;
	char *tmp;
	FILE *fp;

	path = uri_filename(directory, rank);
	D_ASPRINTF(tmp, "%s.tmp", path);
	D_ASSERT(tmp != NULL);

	fp = fopen(tmp, "w");
	D_ASSERTF(fp != NULL, "Cannot open %s\n", tmp);
	D_ASSERT(fprintf(fp, "%s\n", uri) > 0);
	D_ASSERT(fclose(fp) == 0);

	/* Publish only complete URI files while the peer polls for them. */
	D_ASSERT(rename(tmp, path) == 0);

	D_FREE(tmp);
	D_FREE(path);
}

static void
read_peer_uri(const char *directory, d_rank_t rank, char *uri, size_t size)
{
	char *path;
	FILE *fp = NULL;
	int   i;

	path = uri_filename(directory, rank);
	for (i = 0; i < 30 && fp == NULL; i++) {
		fp = fopen(path, "r");
		if (fp == NULL)
			sleep(1);
	}

	D_ASSERTF(fp != NULL, "Timed out waiting for %s\n", path);
	D_ASSERT(fgets(uri, size, fp) != NULL);
	D_ASSERT(fclose(fp) == 0);

	uri[strcspn(uri, "\r\n")] = '\0';
	D_ASSERT(strlen(uri) > 0);

	D_FREE(path);
}

static void
save_secondary_info(const char *directory, const char *remote_uri)
{
	char *path;
	char *tmp;
	FILE *fp;

	D_ASPRINTF(path, "%s/%s.attach_info_tmp", directory, GROUP_NAME);
	D_ASSERT(path != NULL);
	D_ASPRINTF(tmp, "%s.tmp", path);
	D_ASSERT(tmp != NULL);

	fp = fopen(tmp, "w");
	D_ASSERTF(fp != NULL, "Cannot open %s\n", tmp);

	/* Secondary-only clients attach using the server ranks' secondary addresses. */
	D_ASSERT(fprintf(fp, "name %s\nsize 2\nall\n0 %s\n1 %s\n", GROUP_NAME, secondary_uri,
			 remote_uri) > 0);
	D_ASSERT(fclose(fp) == 0);
	D_ASSERT(rename(tmp, path) == 0);

	D_FREE(tmp);
	D_FREE(path);
}

static void *
server_progress(void *arg)
{
	crt_context_t ctx = *(crt_context_t *)arg;
	int           rc;

	do {
		rc = crt_progress(ctx, 1000);
		D_ASSERTF(rc == 0 || rc == -DER_TIMEDOUT, "Progress failed: %d\n", rc);
	} while (atomic_load(&crtu_get_opts()->shutdown) == 0 || rc == 0);

	return NULL;
}

static int
server(const char *workdir, const char *primary_dir, const char *secondary_dir, char *provider,
       char *interface, char *domain, d_rank_t rank)
{
	crt_init_options_t options = {0};
	pthread_t          threads[2];
	crt_group_t       *group;
	struct rpc_result  result;
	char               peer_uri[URI_SIZE];
	char              *primary_uri;
	int                rc;
	int                i;

	self_rank = rank;
	crtu_test_init(rank, 20, true, true);
	crtu_set_shutdown_delay(0);
	D_ASSERT(d_log_init() == 0);
	DBG_PRINT("Starting rank %u\n", rank);

	options.cio_provider  = provider;
	options.cio_interface = interface;
	options.cio_domain    = domain;
	D_ASSERT(crt_init_opt(GROUP_NAME, CRT_FLAG_BIT_SERVER | CRT_FLAG_BIT_AUTO_SWIM_DISABLE,
			      &options) == 0);

	D_ASSERT(crt_group_config_path_set(primary_dir) == 0);
	D_ASSERT(crt_rank_self_set(rank, 1) == 0);
	group = crt_group_lookup(NULL);
	D_ASSERT(group != NULL);

	D_ASSERT(crt_context_create(&contexts[0]) == 0);
	D_ASSERT(crt_context_create_secondary(&contexts[1], 0) == 0);

	D_ASSERT(crt_context_uri_get(contexts[0], &primary_uri) == 0);
	D_ASSERT(crt_context_uri_get(contexts[1], &secondary_uri) == 0);
	DBG_PRINT("Rank %u listening on primary URI: %s\n", rank, primary_uri);
	DBG_PRINT("Rank %u listening on secondary URI: %s\n", rank, secondary_uri);

	D_ASSERT(crt_proto_register(&protocol) == 0);
	for (i = 0; i < 2; i++)
		D_ASSERT(pthread_create(&threads[i], NULL, server_progress, &contexts[i]) == 0);

	publish_uri(workdir, rank, primary_uri);
	D_FREE(primary_uri);

	read_peer_uri(workdir, 1 - rank, peer_uri, sizeof(peer_uri));
	D_ASSERT(crt_group_primary_rank_add(contexts[0], group, 1 - rank, peer_uri) == 0);

	if (rank == 0) {
		/* The peer's URI can be published before its progress thread is ready. */
		for (i = 0; i < 10; i++) {
			rc = send_rpc(contexts[0], group, 1, OP_GET_URI, 0, &result);
			if (rc == 0)
				break;
			sleep(1);
		}
		D_ASSERTF(rc == 0, "Secondary URI request failed: %d\n", rc);

		/* Rank 0 publishes both attach files once both server addresses are known. */
		D_ASSERT(crt_group_config_save(group, true) == 0);
		save_secondary_info(secondary_dir, result.uri);
	}

	for (i = 0; i < 2; i++)
		D_ASSERT(pthread_join(threads[i], NULL) == 0);
	for (i = 0; i < 2; i++)
		D_ASSERT(crt_context_destroy(contexts[i], 1) == 0);

	D_FREE(secondary_uri);
	D_ASSERT(crt_finalize() == 0);
	d_log_fini();

	return 0;
}

static int
client(const char *attach_dir, char *provider, char *interface, char *domain, bool secondary,
       bool shutdown)
{
	crt_init_options_t options = {0};
	crt_context_t      ctx;
	pthread_t          thread;
	crt_group_t       *group;
	struct rpc_result  result;
	d_rank_t           rank;
	int                rc;

	crtu_test_init(0, 20, false, true);
	D_ASSERT(d_log_init() == 0);

	/* The client has one local provider; this flag selects the remote provider index. */
	D_ASSERT(setenv("CRT_SECONDARY_PROVIDER", secondary ? "1" : "0", 1) == 0);

	options.cio_provider  = provider;
	options.cio_interface = interface;
	options.cio_domain    = domain;
	D_ASSERT(crt_init_opt(NULL, 0, &options) == 0);

	D_ASSERT(crt_group_config_path_set(attach_dir) == 0);
	D_ASSERT(crt_context_create(&ctx) == 0);
	D_ASSERT(crt_proto_register(&protocol) == 0);
	D_ASSERT(crt_group_attach(GROUP_NAME, &group) == 0);
	D_ASSERT(pthread_create(&thread, NULL, crtu_progress_fn, &ctx) == 0);

	for (rank = 0; rank < 2; rank++) {
		DBG_PRINT("Sending %s RPC to rank %u over %s provider %s (interface %s)\n",
			  shutdown ? "shutdown" : "ping", rank, secondary ? "secondary" : "primary",
			  provider, interface);
		rc = send_rpc(ctx, group, rank, shutdown ? OP_SHUTDOWN : OP_PING, secondary ? 1 : 0,
			      &result);
		D_ASSERTF(rc == 0, "RPC to rank %u failed: %d\n", rank, rc);

		if (!shutdown) {
			D_ASSERTF(result.provider == (secondary ? 1U : 0U) && result.rank == rank,
				  "Unexpected reply from rank %u: rank %u provider %u\n", rank,
				  result.rank, result.provider);
			printf("RPC passed: rank %u provider %u\n", rank, result.provider);
		}
	}

	if (secondary && !shutdown)
		send_forwarded_bulk(ctx, group);

	crtu_progress_stop();
	D_ASSERT(pthread_join(thread, NULL) == 0);
	D_ASSERT(crt_group_detach(group) == 0);
	D_ASSERT(crt_finalize() == 0);
	d_log_fini();
	return 0;
}

int
main(int argc, char **argv)
{
	char *workdir       = NULL;
	char *primary_dir   = NULL;
	char *secondary_dir = NULL;
	char *provider      = NULL;
	char *interface     = NULL;
	char *domain        = NULL;
	int   rank          = -1;
	bool  secondary     = false;
	bool  shutdown      = false;
	int   opt;

	while ((opt = getopt(argc, argv, "w:a:b:p:i:d:r:sq")) != -1) {
		switch (opt) {
		case 'w':
			workdir = optarg;
			break;
		case 'a':
			primary_dir = optarg;
			break;
		case 'b':
			secondary_dir = optarg;
			break;
		case 'p':
			provider = optarg;
			break;
		case 'i':
			interface = optarg;
			break;
		case 'd':
			domain = optarg;
			break;
		case 'r':
			rank = atoi(optarg);
			break;
		case 's':
			secondary = true;
			break;
		case 'q':
			shutdown = true;
			break;
		default:
			return 1;
		}
	}
	if (provider == NULL || interface == NULL || primary_dir == NULL ||
	    (rank >= 0 && (rank > 1 || workdir == NULL || secondary_dir == NULL))) {
		fprintf(stderr,
			"Usage: %s -p provider -i interface [-d domain] -a attach_dir "
			"[-s] [-q] [-r rank -w workdir -b secondary_dir]\n",
			argv[0]);
		return 1;
	}
	if (rank >= 0)
		return server(workdir, primary_dir, secondary_dir, provider, interface, domain,
			      rank);
	return client(primary_dir, provider, interface, domain, secondary, shutdown);
}
