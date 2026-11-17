/*
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#include <cart/api.h>
#include <gurt/debug.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

/* Public API test: two processes, real CaRT dispatch and real RC endpoints.
 * No native private APIs or test doubles. Keep the server running until the
 * client exits, then interrupt it to exercise context teardown.
 */
#define BASE  0x23000000
#define BYTES 4096
#define CHECK(call)                                                                                \
	do {                                                                                       \
		int rc_ = (call);                                                                  \
		if (rc_) {                                                                         \
			fprintf(stderr, "%s failed: %d at line %d\n", #call, rc_, __LINE__);       \
			abort();                                                                   \
		}                                                                                  \
	} while (0)

struct input {
	uint64_t   sequence;
	crt_bulk_t bulk;
};
struct output {
	uint64_t sequence;
};
static volatile sig_atomic_t stopping;
static unsigned              active;

static int
input_proc(crt_proc_t proc, void *arg)
{
	struct input *in = arg;
	crt_proc_op_t op;
	int           rc = crt_proc_get_op(proc, &op);
	if (!rc)
		rc = crt_proc_uint64_t(proc, op, &in->sequence);
	if (!rc)
		rc = crt_proc_crt_bulk_t(proc, op, &in->bulk);
	return rc;
}
static int
output_proc(crt_proc_t proc, void *arg)
{
	struct output *out = arg;
	crt_proc_op_t  op;
	int            rc = crt_proc_get_op(proc, &op);
	return rc ? rc : crt_proc_uint64_t(proc, op, &out->sequence);
}
struct operation {
	struct crt_bulk_desc desc;
	unsigned char        data[BYTES];
};

static void
respond(crt_rpc_t *rpc)
{
	struct input  *in  = crt_req_get(rpc);
	struct output *out = crt_reply_get(rpc);
	out->sequence      = in->sequence + 1;
	CHECK(crt_reply_send(rpc));
}

static int
bulk_done(const struct crt_bulk_cb_info *info)
{
	struct operation *op = info->bci_arg;
	CHECK(info->bci_rc);
	if (op->desc.bd_bulk_op == CRT_BULK_GET) {
		for (unsigned i = 0; i < BYTES; i++)
			if (op->data[i] != 0x5a)
				abort();
		memset(op->data, 0xa5, BYTES);
		op->desc.bd_bulk_op = CRT_BULK_PUT;
		CHECK(crt_bulk_transfer(&op->desc, bulk_done, op, NULL));
	} else {
		respond(op->desc.bd_rpc);
		CHECK(crt_bulk_free(op->desc.bd_local_hdl));
		crt_req_decref(op->desc.bd_rpc);
		active--;
		free(op);
	}
	return 0;
}

static void
handler(crt_rpc_t *rpc)
{
	struct input *in = crt_req_get(rpc);
	if (in->bulk == CRT_BULK_NULL) {
		respond(rpc);
		return;
	}
	struct operation *op = calloc(1, sizeof(*op));
	if (!op)
		abort();
	size_t length;
	CHECK(crt_bulk_get_len(in->bulk, &length));
	if (length != BYTES)
		abort();
	d_iov_t iov;
	d_iov_set(&iov, op->data, BYTES);
	d_sg_list_t sgl = {.sg_nr = 1, .sg_nr_out = 1, .sg_iovs = &iov};
	op->desc        = (struct crt_bulk_desc){
		   .bd_rpc = rpc, .bd_remote_hdl = in->bulk, .bd_bulk_op = CRT_BULK_GET, .bd_len = BYTES};
	CHECK(crt_bulk_create(rpc->cr_ctx, &sgl, CRT_BULK_RW, &op->desc.bd_local_hdl));
	crt_req_addref(rpc);
	active++;
	CHECK(crt_bulk_transfer(&op->desc, bulk_done, op, NULL));
}
static void
no_reply_handler(crt_rpc_t *rpc)
{
	(void)rpc; /* CaRT must return -DER_NOREPLY, including native error framing. */
}
static struct crt_req_format       format    = {.crf_proc_in  = input_proc,
						.crf_proc_out = output_proc,
						.crf_size_in  = sizeof(struct input),
						.crf_size_out = sizeof(struct output)};
static struct crt_proto_rpc_format formats[] = {{.prf_req_fmt = &format, .prf_hdlr = handler},
						{.prf_hdlr = no_reply_handler}};
static struct crt_proto_format     protocol  = {
	 .cpf_name = "native_smoke", .cpf_ver = 0, .cpf_count = 2, .cpf_prf = formats, .cpf_base = BASE};
struct completion {
	bool done;
	int  expected;
};
static void
done(const struct crt_cb_info *info)
{
	struct completion *c = info->cci_arg;
	if (info->cci_rc != c->expected) {
		fprintf(stderr, "RPC returned %d, expected %d\n", info->cci_rc, c->expected);
		abort();
	}
	if (!c->expected) {
		struct input  *in  = crt_req_get(info->cci_rpc);
		struct output *out = crt_reply_get(info->cci_rpc);
		if (out->sequence != in->sequence + 1)
			abort();
	}
	c->done = true;
}
static void
progress(crt_context_t ctx)
{
	int rc = crt_progress(ctx, 0);
	if (rc != -DER_TIMEDOUT)
		CHECK(rc);
}
static void
interrupt(int signal)
{
	(void)signal;
	stopping = 1;
}

int
main(int argc, char **argv)
{
	if (argc < 2 || (strcmp(argv[1], "server") && strcmp(argv[1], "client")) ||
	    (!strcmp(argv[1], "client") && argc != 3)) {
		fprintf(stderr, "usage: %s server | client doca+rc://SERVER_IPV4:PORT\n", argv[0]);
		return 2;
	}
	bool           server = !strcmp(argv[1], "server");
	unsigned char *data   = NULL;
	CHECK(d_log_init());
	CHECK(crt_init(server ? "native_smoke" : NULL,
		       server ? CRT_FLAG_BIT_SERVER | CRT_FLAG_BIT_AUTO_SWIM_DISABLE : 0));
	CHECK(crt_proto_register(&protocol));
	if (server)
		CHECK(crt_rank_self_set(0, 1));
	crt_context_t ctx;
	CHECK(crt_context_create(&ctx));
	if (server) {
		char *uri;
		CHECK(crt_context_uri_get(ctx, &uri));
		printf("Server URI: %s\n", uri);
		fflush(stdout);
		free(uri);
		signal(SIGINT, interrupt);
		signal(SIGTERM, interrupt);
		while (!stopping || active)
			progress(ctx);
	} else {
		crt_group_t *group;
		CHECK(crt_group_view_create("native_smoke", &group));
		CHECK(crt_group_primary_rank_add(ctx, group, 0, argv[2]));
		crt_endpoint_t endpoint = {.ep_grp = group, .ep_rank = 0, .ep_tag = 0};
		d_iov_t        iov;
		data = malloc(BYTES);
		if (data == NULL)
			abort();
		d_iov_set(&iov, data, BYTES);
		d_sg_list_t sgl = {.sg_nr = 1, .sg_nr_out = 1, .sg_iovs = &iov};
		crt_bulk_t  bulk;
		CHECK(crt_bulk_create(ctx, &sgl, CRT_BULK_RW, &bulk));
		for (unsigned i = 0; i <= 1024; i++) {
			crt_rpc_t        *rpc;
			struct completion c = {.expected = i == 1024 ? -DER_NOREPLY : 0};
			CHECK(crt_req_create(ctx, &endpoint, BASE + (i == 1024), &rpc));
			if (i < 1024) {
				struct input *in = crt_req_get(rpc);
				in->sequence     = i;
				in->bulk         = i & 1 ? bulk : CRT_BULK_NULL;
				memset(data, 0x5a, BYTES);
			}
			CHECK(crt_req_send(rpc, done, &c));
			while (!c.done)
				progress(ctx);
			if (i < 1024 && (i & 1))
				for (unsigned j = 0; j < BYTES; j++)
					if (data[j] != 0xa5)
						abort();
		}
		CHECK(crt_bulk_free(bulk));
		CHECK(crt_group_view_destroy(group));
		puts("CaRT native smoke: 1024 RPCs, 512 bulk GET/PUT round trips, error reply "
		     "passed");
	}
	/* Busy means registered memory or CQEs still belong to the transport. */
	int      rc;
	unsigned attempt = 0;
	while ((rc = crt_context_destroy(ctx, 0)) == -DER_BUSY || rc == -DER_AGAIN) {
		if (++attempt == 100000) {
			fprintf(stderr, "teardown did not drain\n");
			abort();
		}
		progress(ctx);
	}
	CHECK(rc);
	/* The native registration cache can retain the bulk until context teardown. */
	free(data);
	CHECK(crt_finalize());
	d_log_fini();
	return 0;
}
