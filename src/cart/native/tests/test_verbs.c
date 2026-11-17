/*
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#include "../transport.h"
#include <infiniband/verbs.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned           partial_after;
static struct ibv_device  device;
static struct ibv_device *devices[] = {&device, NULL};
static struct ibv_context context;
static struct ibv_pd      pd;
static struct ibv_cq      cq;
static struct ibv_qp      qp = {.qp_num = 7};
static struct ibv_wc      completions[256];
static unsigned           head, tail;
static uint64_t           accepted[256];
static unsigned           num_accepted;
static int                post_error, poll_error;
static unsigned           last_send_flags;
static unsigned           poll_calls;
static int                create_qp_error;
static int                destroy_qp_error;
static int                modify_qp_error;
static int                open_device_error, alloc_pd_error, create_cq_error, register_error;
static unsigned           destroyed_qps, destroyed_cqs, destroyed_pds, closed_devices;

struct ibv_device **
ibv_get_device_list(int *count)
{
	*count = 1;
	return devices;
}
const char *
ibv_get_device_name(struct ibv_device *d)
{
	(void)d;
	return "mock0";
}
void
ibv_free_device_list(struct ibv_device **d)
{
	(void)d;
}
struct ibv_context *
ibv_open_device(struct ibv_device *d)
{
	(void)d;
	if (open_device_error) {
		errno = open_device_error;
		return NULL;
	}
	return &context;
}
int
ibv_close_device(struct ibv_context *c)
{
	(void)c;
	closed_devices++;
	return 0;
}
static unsigned configured_reads, configured_responders;
int
ibv_query_device(struct ibv_context *context_arg, struct ibv_device_attr *caps)
{
	(void)context_arg;
	*caps = (struct ibv_device_attr){.max_qp_rd_atom = 8, .max_qp_init_rd_atom = 16};
	return 0;
}
int
ibv_query_port(struct ibv_context *c, uint8_t p, struct ibv_port_attr *a)
{
	(void)c;
	(void)p;
	*a = (struct ibv_port_attr){.state = IBV_PORT_ACTIVE, .active_mtu = IBV_MTU_1024, .lid = 1};
	return 0;
}
int
ibv_query_gid(struct ibv_context *c, uint8_t p, int i, union ibv_gid *g)
{
	(void)c;
	(void)p;
	(void)i;
	memset(g, 1, sizeof(*g));
	return 0;
}
struct ibv_pd *
ibv_alloc_pd(struct ibv_context *c)
{
	(void)c;
	if (alloc_pd_error) {
		errno = alloc_pd_error;
		return NULL;
	}
	return &pd;
}
int
ibv_dealloc_pd(struct ibv_pd *p)
{
	(void)p;
	destroyed_pds++;
	return 0;
}
struct ibv_cq *
ibv_create_cq(struct ibv_context *c, int n, void *a, void *b, int v)
{
	if (create_cq_error) {
		errno = create_cq_error;
		return NULL;
	}
	(void)c;
	(void)n;
	(void)a;
	(void)b;
	(void)v;
	return &cq;
}
int
ibv_destroy_cq(struct ibv_cq *c)
{
	(void)c;
	destroyed_cqs++;
	return 0;
}
struct ibv_qp *
ibv_create_qp(struct ibv_pd *p, struct ibv_qp_init_attr *a)
{
	(void)p;
	if (a->cap.max_inline_data > 128) {
		errno = EINVAL;
		return NULL;
	}
	if (create_qp_error) {
		errno = ENOMEM;
		return NULL;
	}
	return &qp;
}
int
ibv_destroy_qp(struct ibv_qp *q)
{
	(void)q;
	if (destroy_qp_error)
		return destroy_qp_error;
	destroyed_qps++;
	return 0;
}
int
ibv_modify_qp(struct ibv_qp *q, struct ibv_qp_attr *a, int mask)
{
	if (modify_qp_error)
		return modify_qp_error;
	(void)q;
	(void)mask;
	if (mask & IBV_QP_MAX_QP_RD_ATOMIC)
		configured_reads = a->max_rd_atomic;
	if (mask & IBV_QP_MAX_DEST_RD_ATOMIC)
		configured_responders = a->max_dest_rd_atomic;
	if (a->qp_state == IBV_QPS_ERR) {
		while (num_accepted != 0)
			completions[tail++] = (struct ibv_wc){.wr_id  = accepted[--num_accepted],
							      .status = IBV_WC_WR_FLUSH_ERR};
	}
	return 0;
}
struct ibv_mr *
ibv_reg_mr(struct ibv_pd *p, void *a, size_t n, int access)
{
	if (register_error) {
		errno = register_error;
		return NULL;
	}
	struct ibv_mr *mr = malloc(sizeof(*mr));
	(void)p;
	(void)access;
	assert(mr != NULL);
	*mr = (struct ibv_mr){.addr = a, .length = n, .lkey = 2, .rkey = 3};
	return mr;
}
int
ibv_dereg_mr(struct ibv_mr *mr)
{
	free(mr);
	return 0;
}
int
ibv_post_send(struct ibv_qp *q, struct ibv_send_wr *wr, struct ibv_send_wr **bad)
{
	(void)q;
	(void)bad;
	if (post_error != 0)
		return post_error;
	unsigned sent = 0;
	for (; wr; wr = wr->next) {
		if (partial_after && sent == partial_after) {
			*bad          = wr;
			partial_after = 0;
			return EAGAIN;
		}
		sent++;
		last_send_flags = wr->send_flags;

		accepted[num_accepted++] = wr->wr_id;
	}
	return 0;
}
int
ibv_post_recv(struct ibv_qp *q, struct ibv_recv_wr *wr, struct ibv_recv_wr **bad)
{
	(void)q;
	(void)bad;
	if (post_error)
		return post_error;
	for (; wr; wr = wr->next)
		accepted[num_accepted++] = wr->wr_id;
	return 0;
}
int
ibv_poll_cq(struct ibv_cq *c, int n, struct ibv_wc *wc)
{
	(void)c;
	poll_calls++;
	if (poll_error) {
		poll_error = 0;
		return -1;
	}
	int count = 0;
	while (head != tail && count < n)
		wc[count++] = completions[head++];
	return count;
}

static void
test_lifetime(void)
{
	const struct crt_native_ops *ops = &crt_native_verbs_ops;
	struct crt_native_config  cfg = {.device = "mock0", .port = 1, .gid_index = -1, .depth = 2};
	struct crt_native_address address;
	struct crt_native_ep     *ep;
	struct crt_native_mr     *mr;
	struct crt_native_remote_memory remote;
	struct crt_native_completion    out[2];
	unsigned                        count;
	char                            buffer[64];

	assert(ops->open(&cfg, &ep) == 0);
	assert(ops->quiesce(ep) == EINVAL);
	assert(ops->address(ep, &address) == 0);
	assert(ops->register_memory(ep, buffer, sizeof(buffer),
				    CRT_NATIVE_REMOTE_READ | CRT_NATIVE_REMOTE_WRITE, &mr,
				    &remote) == 0);
	assert(ops->post(ep, CRT_NATIVE_SEND, mr, 0, 8, NULL, 0, NULL) == ENOTCONN);
	/* Receives may be preposted before QP connection. */
	assert(ops->post(ep, CRT_NATIVE_RECV, mr, 0, 8, NULL, 0, buffer) == 0);
	assert(ops->connect(ep, &address) == 0);
	post_error = ENOMEM;
	assert(ops->post(ep, CRT_NATIVE_SEND, mr, 8, 8, NULL, 0, buffer + 8) == ENOMEM);
	post_error = 0;
	assert(ops->post(ep, CRT_NATIVE_WRITE, mr, 8, 8, &remote, 8, buffer + 8) == 0);
	assert(ops->post(ep, CRT_NATIVE_SEND, mr, 8, 8, NULL, 0, NULL) == EAGAIN);
	assert(ops->post(ep, CRT_NATIVE_READ, mr, 0, 8, &remote, 60, NULL) == EINVAL);
	assert(ops->post(ep, CRT_NATIVE_SEND, mr, SIZE_MAX, 8, NULL, 0, NULL) == EINVAL);
	assert(ops->deregister_memory(mr) == EBUSY);
	assert(ops->close(ep) == EBUSY);
	assert(ops->progress(ep, out, 2, &count) == 0 && count == 0);
	/* A CQ polling error initiates flushing, but must not release any buffers. */
	poll_error = 1;
	assert(ops->progress(ep, out, 2, &count) == EIO && count == 0);
	assert(ops->deregister_memory(mr) == EBUSY);
	assert(ops->close(ep) == EBUSY);
	assert(ops->post(ep, CRT_NATIVE_SEND, mr, 0, 8, NULL, 0, NULL) == ESHUTDOWN);
	assert(ops->progress(ep, out, 1, &count) == 0 && count == 1);
	assert(out[0].error == ECANCELED && out[0].bytes == 0);
	assert(ops->quiesce(ep) == EBUSY);
	assert(ops->deregister_memory(mr) == EBUSY);
	assert(ops->progress(ep, out, 2, &count) == 0 && count == 1);
	assert(out[0].error == ECANCELED);
	destroy_qp_error = EIO;
	assert(ops->quiesce(ep) == EIO);
	destroy_qp_error = 0;
	assert(ops->quiesce(ep) == 0);
	assert(ops->quiesce(ep) == 0 && ops->close_begin(ep) == 0);
	assert(ops->close(ep) == EBUSY); /* MR remains reachable after path teardown */
	assert(ops->deregister_memory(mr) == 0);
	assert(ops->close(ep) == 0);
	assert(destroyed_qps == 1 && destroyed_cqs == 1 && destroyed_pds == 1 &&
	       closed_devices == 1);
}

static void
test_success_and_open_failure(void)
{
	const struct crt_native_ops *ops = &crt_native_verbs_ops;
	struct crt_native_config  cfg = {.device = "mock0", .port = 1, .gid_index = -1, .depth = 4};
	struct crt_native_ep     *ep;
	struct crt_native_address address;
	struct crt_native_mr     *mr;
	struct crt_native_remote_memory remote;
	struct crt_native_completion    out[4];
	unsigned                        count;
	char                            buffer[64];

	head = tail = num_accepted = 0;
	assert(ops->open(&cfg, &ep) == 0);
	assert(ops->address(ep, &address) == 0);
	assert(ops->connect(ep, &address) == 0);
	assert(ops->register_memory(ep, buffer, sizeof(buffer),
				    CRT_NATIVE_REMOTE_READ | CRT_NATIVE_REMOTE_WRITE, &mr,
				    &remote) == 0);
	for (int i = CRT_NATIVE_SEND; i <= CRT_NATIVE_WRITE; i++) {
		assert(ops->post(ep, i, mr, 0, 8, &remote, 0, buffer + i) == 0);
		completions[tail++] = (struct ibv_wc){
		    .wr_id = accepted[--num_accepted], .status = IBV_WC_SUCCESS, .byte_len = 4};
	}
	poll_calls = 0;
	assert(ops->progress(ep, out, 4, &count) == 0 && count == 4);
	assert(poll_calls == 1);
	for (unsigned i = 0; i < count; i++) {
		assert(out[i].cookie == buffer + i && out[i].error == 0);
		assert(out[i].bytes == (i == CRT_NATIVE_RECV ? 4 : 8));
	}
	assert(ops->deregister_memory(mr) == 0);
	assert(ops->close_begin(ep) == 0);
	assert(ops->close(ep) == 0);
	create_qp_error = 1;
	assert(ops->open(&cfg, &ep) == ENOMEM && ep == NULL);
	assert(destroyed_qps == 2 && destroyed_cqs == 3 && destroyed_pds == 3 &&
	       closed_devices == 3);
	create_qp_error  = 0;
	modify_qp_error  = EINVAL;
	destroy_qp_error = EIO;
	assert(ops->open(&cfg, &ep) == EINVAL && ep != NULL);
	modify_qp_error = destroy_qp_error = 0;
	assert(ops->close(ep) == 0);
	assert(destroyed_qps == 3 && destroyed_cqs == 4 && destroyed_pds == 4 &&
	       closed_devices == 4);
	assert(ops->open(NULL, &ep) == EINVAL && ep == NULL);
}

static void
test_slot_recycling(void)
{
	const struct crt_native_ops *ops = &crt_native_verbs_ops;
	struct crt_native_config  cfg = {.device = "mock0", .port = 1, .gid_index = -1, .depth = 4};
	struct crt_native_ep     *ep;
	struct crt_native_address address;
	struct crt_native_mr     *mr;
	struct crt_native_remote_memory remote;
	struct crt_native_completion    out[4];
	char                            buffer[64];
	unsigned                        count;

	create_qp_error = 0;
	assert(ops->open(&cfg, &ep) == 0);
	assert(ops->address(ep, &address) == 0);
	assert(ops->connect(ep, &address) == 0);
	assert(ops->register_memory(ep, buffer, sizeof(buffer), false, &mr, &remote) == 0);
	for (unsigned iteration = 0; iteration < 1024; iteration++) {
		head = tail = num_accepted = 0;
		/* A rejected post must not consume a free-list entry. */
		post_error = EAGAIN;
		assert(ops->post(ep, CRT_NATIVE_SEND, mr, 0, 8, NULL, 0, NULL) == EAGAIN);
		post_error = 0;
		for (unsigned i = 0; i < cfg.depth; i++) {
			assert(ops->post(ep, CRT_NATIVE_SEND, mr, 0, 8, NULL, 0, buffer + i) == 0);
			for (unsigned j = 0; j < i; j++)
				assert(accepted[j] != accepted[i]);
		}
		assert(ops->post(ep, CRT_NATIVE_SEND, mr, 0, 8, NULL, 0, NULL) == EAGAIN);
		/* Recycling must not assume posting-order completions. */
		while (num_accepted != 0)
			completions[tail++] = (struct ibv_wc){.wr_id  = accepted[--num_accepted],
							      .status = IBV_WC_SUCCESS};
		poll_calls = 0;
		assert(ops->progress(ep, out, 4, &count) == 0 && count == 4);
		assert(poll_calls == 1);
		for (unsigned i = 0; i < count; i++)
			assert(out[i].cookie == buffer + 3 - i && out[i].error == 0);
	}
	assert(ops->deregister_memory(mr) == 0);
	assert(ops->close_begin(ep) == 0);
	assert(ops->close(ep) == 0);
}

static void
test_allocation_failures(void)
{
	const struct crt_native_ops *ops = &crt_native_verbs_ops;
	struct crt_native_config cfg = {.device = "mock0", .port = 1, .gid_index = -1, .depth = 4};
	struct crt_native_ep    *ep  = NULL;
	open_device_error            = EACCES;
	assert(ops->open(&cfg, &ep) == EACCES && ep == NULL);
	open_device_error = 0;
	alloc_pd_error    = ENOMEM;
	assert(ops->open(&cfg, &ep) == ENOMEM && ep == NULL);
	alloc_pd_error  = 0;
	create_cq_error = ENOMEM;
	assert(ops->open(&cfg, &ep) == ENOMEM && ep == NULL);
	create_cq_error = 0;
	assert(ops->open(&cfg, &ep) == 0);
	struct crt_native_mr           *mr;
	struct crt_native_remote_memory remote;
	char                            buffer[64];
	register_error = ENOMEM;
	assert(ops->register_memory(ep, buffer, sizeof(buffer), CRT_NATIVE_REMOTE_READ, &mr,
				    &remote) == ENOMEM &&
	       mr == NULL);
	register_error = 0;
	assert(ops->close(ep) == 0);
}

static void
woken(void *arg)
{
	(*(unsigned *)arg)++;
}

static void
test_shared_cq(void)
{
	const struct crt_native_ops *ops = &crt_native_verbs_ops;
	struct crt_native_config     cfg = {
		.device = "mock0", .port = 1, .gid_index = -1, .depth = 4, .wake = woken};
	struct crt_native_group        *g;
	struct crt_native_ep           *a, *b;
	struct crt_native_mr           *amr, *bmr;
	struct crt_native_remote_memory remote;
	struct crt_native_address       address;
	struct crt_native_completion    out[4];
	char                            abuf[16], bbuf[16];
	unsigned                        count, awakes = 0, bwakes = 0;
	head = tail = num_accepted = 0;
	unsigned cq_before = destroyed_cqs, device_before = closed_devices;
	assert(ops->group_create(&cfg, 2, &g) == 0);
	cfg.group    = g;
	cfg.wake_arg = &awakes;
	assert(ops->open(&cfg, &a) == 0);
	assert(ops->close(a) == 0);
	assert(ops->open(&cfg, &a) == 0);
	cfg.wake_arg = &bwakes;
	assert(ops->open(&cfg, &b) == 0);
	assert(ops->group_destroy(g) == EBUSY);
	assert(ops->address(a, &address) == 0 && ops->connect(a, &address) == 0);
	assert(ops->address(b, &address) == 0 && ops->connect(b, &address) == 0);
	assert(ops->register_memory(a, abuf, sizeof(abuf), 0, &amr, &remote) == 0);
	assert(ops->register_memory(b, bbuf, sizeof(bbuf), 0, &bmr, &remote) == 0);
	assert(ops->post(a, CRT_NATIVE_SEND, amr, 0, 8, NULL, 0, abuf) == 0);
	assert(ops->post(b, CRT_NATIVE_SEND, bmr, 0, 8, NULL, 0, bbuf) == 0);
	assert(num_accepted == 2);
	/* Interleave endpoint completions on one hardware CQ. */
	completions[tail++]   = (struct ibv_wc){.wr_id = accepted[1], .status = IBV_WC_SUCCESS};
	completions[tail++]   = (struct ibv_wc){.wr_id = accepted[0], .status = IBV_WC_SUCCESS};
	num_accepted          = 0;
	unsigned polls_before = poll_calls;
	awakes = bwakes = 0;
	assert(ops->group_progress(g) == 0);
	assert(poll_calls == polls_before + 1 && awakes == 1 && bwakes == 1);
	assert(ops->deregister_memory(amr) == EBUSY);
	assert(ops->deregister_memory(bmr) == EBUSY);
	assert(ops->progress(a, out, 4, &count) == 0 && count == 1);
	assert(out[0].cookie == abuf && out[0].error == 0 && out[0].bytes == 8);
	assert(ops->progress(b, out, 4, &count) == 0 && count == 1);
	assert(out[0].cookie == bbuf && out[0].error == 0 && out[0].bytes == 8);
	assert(poll_calls == polls_before + 1); /* Endpoint progress never polls the CQ. */
	/* A shared CQ error must still allow later flush completions to drain. */
	assert(ops->post(a, CRT_NATIVE_SEND, amr, 0, 8, NULL, 0, abuf) == 0);
	assert(ops->post(b, CRT_NATIVE_SEND, bmr, 0, 8, NULL, 0, bbuf) == 0);
	poll_error = 1;
	assert(ops->group_progress(g) == EIO);
	assert(ops->close_begin(a) == 0 && ops->close_begin(b) == 0);
	assert(ops->quiesce(a) == EBUSY && ops->quiesce(b) == EBUSY);
	assert(ops->group_progress(g) == EIO);
	assert(ops->progress(a, out, 4, &count) == EIO && count == 1);
	assert(out[0].cookie == abuf && out[0].error == ECANCELED);
	assert(ops->progress(b, out, 4, &count) == EIO && count == 1);
	assert(out[0].cookie == bbuf && out[0].error == ECANCELED);
	assert(ops->deregister_memory(amr) == 0);
	assert(ops->deregister_memory(bmr) == 0);
	assert(ops->close_begin(a) == 0 && ops->quiesce(a) == 0);
	assert(ops->close(a) == 0);
	assert(destroyed_cqs == cq_before && closed_devices == device_before);
	/* Failed groups reject new endpoints while existing handles drain. */
	assert(ops->open(&cfg, &a) == EIO && a == NULL);
	assert(ops->close(b) == 0);
	assert(ops->group_destroy(g) == 0);
	assert(destroyed_cqs == cq_before + 1 && closed_devices == device_before + 1);
}

static void
test_read_credits(void)
{
	const struct crt_native_ops *ops = &crt_native_verbs_ops;
	struct crt_native_config     cfg = {
		.device = "mock0", .port = 1, .gid_index = -1, .depth = 8, .rdma_read_depth = 12};
	struct crt_native_ep           *ep;
	struct crt_native_address       a;
	struct crt_native_mr           *mr;
	struct crt_native_remote_memory remote;
	struct crt_native_completion    out[8];
	unsigned                        count;
	char                            buffer[64];
	head = tail = num_accepted = 0;
	assert(ops->open(&cfg, &ep) == 0 && ops->address(ep, &a) == 0);
	assert(a.read_out == 12 && a.read_in == 8);
	a.read_in = 3;
	assert(ops->connect(ep, &a) == 0);
	assert(configured_reads == 3 && configured_responders == 8);
	assert(ops->register_memory(ep, buffer, sizeof(buffer), CRT_NATIVE_REMOTE_READ, &mr,
				    &remote) == 0);
	for (unsigned i = 0; i < 3; i++)
		assert(ops->post(ep, CRT_NATIVE_READ, mr, 0, 8, &remote, 0, buffer) == 0);
	assert(ops->post(ep, CRT_NATIVE_READ, mr, 0, 8, &remote, 0, buffer) == EAGAIN);
	for (unsigned i = 0; i < num_accepted; i++)
		completions[tail++] =
		    (struct ibv_wc){.wr_id = accepted[i], .status = IBV_WC_SUCCESS};
	num_accepted = 0;
	assert(ops->progress(ep, out, 8, &count) == 0 && count == 3);
	assert(ops->post(ep, CRT_NATIVE_READ, mr, 0, 8, &remote, 0, buffer) == 0);
	assert(ops->close_begin(ep) == 0);
	assert(ops->progress(ep, out, 8, &count) == 0 && count == 1);
	assert(ops->quiesce(ep) == 0 && ops->deregister_memory(mr) == 0 && ops->close(ep) == 0);
}

static void
test_inline_send(void)
{
	const struct crt_native_ops *ops = &crt_native_verbs_ops;
	struct crt_native_config     cfg = {
		.device = "mock0", .port = 1, .gid_index = -1, .depth = 4, .inline_size = 256};
	struct crt_native_ep           *ep;
	struct crt_native_address       address;
	struct crt_native_mr           *mr;
	struct crt_native_remote_memory remote;
	struct crt_native_completion    out[4];
	struct crt_native_stats         stats;
	unsigned                        count;
	char                            buffer[256];
	head = tail = num_accepted = 0;
	assert(ops->open(&cfg, &ep) == 0 && ops->address(ep, &address) == 0);
	assert(ops->connect(ep, &address) == 0);
	assert(ops->register_memory(ep, buffer, sizeof(buffer), 0, &mr, &remote) == 0);
	assert(ops->post(ep, CRT_NATIVE_SEND, mr, 0, 64, NULL, 0, buffer) == 0);
	assert(last_send_flags & IBV_SEND_INLINE);
	assert(ops->post(ep, CRT_NATIVE_SEND, mr, 0, 256, NULL, 0, buffer) == 0);
	assert(!(last_send_flags & IBV_SEND_INLINE));
	ops->get_stats(ep, &stats);
	assert(stats.inline_sends == 1 && stats.send_posts == 2);
	assert(ops->deregister_memory(mr) == EBUSY);
	assert(ops->close_begin(ep) == 0);
	assert(ops->progress(ep, out, 4, &count) == 0 && count == 2);
	assert(ops->deregister_memory(mr) == 0 && ops->close(ep) == 0);
}

static void
test_cq_burst(void)
{
	const struct crt_native_ops *ops = &crt_native_verbs_ops;
	struct crt_native_config cfg = {.device = "mock0", .port = 1, .gid_index = -1, .depth = 64};
	struct crt_native_group *group;
	struct crt_native_ep    *ep;
	struct crt_native_address       address;
	struct crt_native_mr           *mr;
	struct crt_native_remote_memory remote;
	struct crt_native_completion    out[64];
	unsigned                        count;
	char                            buffer[64];
	head = tail = num_accepted = 0;
	assert(ops->group_create(&cfg, 1, &group) == 0);
	cfg.group = group;
	assert(ops->open(&cfg, &ep) == 0 && ops->address(ep, &address) == 0);
	assert(ops->connect(ep, &address) == 0);
	assert(ops->register_memory(ep, buffer, sizeof(buffer), 0, &mr, &remote) == 0);
	for (unsigned i = 0; i < 64; i++) {
		assert(ops->post(ep, CRT_NATIVE_SEND, mr, 0, 8, NULL, 0, buffer) == 0);
		completions[tail++] =
		    (struct ibv_wc){.wr_id = accepted[i], .status = IBV_WC_SUCCESS};
	}
	num_accepted    = 0;
	unsigned before = poll_calls;
	assert(ops->group_progress(group) == 0);
	assert(poll_calls - before == 3);
	assert(ops->progress(ep, out, 64, &count) == 0 && count == 64);
	assert(ops->deregister_memory(mr) == 0 && ops->close(ep) == 0);
	assert(ops->group_destroy(group) == 0);
}

static void
test_linked_posting(void)
{
	const struct crt_native_ops *ops = &crt_native_verbs_ops;
	struct crt_native_config     cfg = {
		.device = "mock0", .port = 1, .gid_index = -1, .depth = 8, .signal_interval = 1};
	struct crt_native_ep           *ep;
	struct crt_native_address       a;
	struct crt_native_mr           *mr;
	struct crt_native_remote_memory remote;
	struct crt_native_completion    out[8];
	struct crt_native_post          posts[3];
	struct crt_native_stats         stats;
	unsigned                        accepted_count, count;
	char                            buffer[64];
	head = tail = num_accepted = 0;
	assert(ops->open(&cfg, &ep) == 0 && ops->address(ep, &a) == 0 && ops->connect(ep, &a) == 0);
	assert(ops->register_memory(ep, buffer, sizeof(buffer), 0, &mr, &remote) == 0);
	for (unsigned i = 0; i < 3; i++)
		posts[i] = (struct crt_native_post){
		    .opcode = CRT_NATIVE_SEND, .mr = mr, .length = 8, .cookie = &buffer[i]};
	posts[1].length = 0;
	assert(ops->post_batch(ep, posts, 3, &accepted_count) == EINVAL && accepted_count == 0);
	posts[1].length = 8;
	partial_after   = 1;
	assert(ops->post_batch(ep, posts, 3, &accepted_count) == EAGAIN && accepted_count == 1);
	assert(ops->deregister_memory(mr) == EBUSY);
	assert(ops->post_batch(ep, posts + 1, 2, &accepted_count) == 0 && accepted_count == 2);
	ops->get_stats(ep, &stats);
	assert(stats.send_batches == 2 && stats.send_posts == 3);
	assert(ops->close_begin(ep) == 0);
	assert(ops->progress(ep, out, 8, &count) == 0 && count == 3);
	assert(ops->deregister_memory(mr) == 0 && ops->close(ep) == 0);
}

static void
test_selective_signaling(void)
{
	for (unsigned grouped = 0; grouped < 2; grouped++)
		for (unsigned partial = 0; partial < 2; partial++) {
			const struct crt_native_ops *ops = &crt_native_verbs_ops;
			struct crt_native_config     cfg = {
				.device = "mock0", .port = 1, .gid_index = -1, .depth = 32};
			struct crt_native_group        *group = NULL;
			struct crt_native_ep           *ep;
			struct crt_native_address       address;
			struct crt_native_mr           *mr;
			struct crt_native_remote_memory remote;
			struct crt_native_post          posts[20];
			struct crt_native_completion    out;
			struct crt_native_stats         stats;
			unsigned                        accepted_count, count;
			char                            buffer[64];
			head = tail = num_accepted = 0;
			if (grouped) {
				assert(ops->group_create(&cfg, 1, &group) == 0);
				cfg.group = group;
			}
			assert(ops->open(&cfg, &ep) == 0 && ops->address(ep, &address) == 0);
			assert(ops->connect(ep, &address) == 0);
			assert(ops->register_memory(ep, buffer, sizeof(buffer), 0, &mr, &remote) ==
			       0);
			for (unsigned i = 0; i < 20; i++)
				posts[i] = (struct crt_native_post){.opcode = CRT_NATIVE_SEND,
								    .mr     = mr,
								    .length = 8,
								    .cookie = &buffer[i]};
			partial_after = partial ? 1 : 0;
			assert(ops->post_batch(ep, posts, 20, &accepted_count) ==
			       (partial ? EIO : 0));
			assert(accepted_count == (partial ? 1 : 20));
			if (partial) {
				assert(ops->post(ep, CRT_NATIVE_SEND, mr, 0, 8, NULL, 0, buffer) ==
				       ESHUTDOWN);
			} else {
				for (unsigned i = 7; i < 20; i += 8)
					completions[tail++] = (struct ibv_wc){
					    .wr_id = accepted[i], .status = IBV_WC_SUCCESS};
				completions[tail++] = (struct ibv_wc){.wr_id  = accepted[19],
								      .status = IBV_WC_SUCCESS};
				num_accepted        = 0;
			}
			if (grouped)
				assert(ops->group_progress(group) == 0);
			for (unsigned i = 0; i < accepted_count; i++) {
				assert(ops->deregister_memory(mr) == EBUSY);
				assert(ops->progress(ep, &out, 1, &count) == 0 && count == 1);
				assert(out.cookie == &buffer[i] &&
				       out.error == (partial ? ECANCELED : 0));
			}
			ops->get_stats(ep, &stats);
			assert(stats.signaled_sends == (partial ? 0 : 3));
			assert(stats.logical_completions == accepted_count);
			assert(ops->deregister_memory(mr) == 0 && ops->close(ep) == 0);
			if (grouped)
				assert(ops->group_destroy(group) == 0);
		}
}

int
main(void)
{
	test_lifetime();
	test_success_and_open_failure();
	test_slot_recycling();
	test_allocation_failures();
	test_shared_cq();
	test_read_credits();
	test_inline_send();
	test_cq_burst();
	test_linked_posting();
	test_selective_signaling();
	puts("native verbs ownership, backpressure, completion and cleanup tests passed");
	return 0;
}
