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

static struct ibv_device  device;
static struct ibv_device *devices[] = {&device, NULL};
static struct ibv_context context;
static struct ibv_pd      pd;
static struct ibv_cq      cq;
static struct ibv_qp      qp = {.qp_num = 7};
static struct ibv_wc      completions[32];
static unsigned           head, tail;
static uint64_t           accepted[32];
static unsigned           num_accepted;
static int                post_error, poll_error;
static unsigned           poll_calls;
static int                create_qp_error;
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
	return &context;
}
int
ibv_close_device(struct ibv_context *c)
{
	(void)c;
	closed_devices++;
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
	(void)a;
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
	destroyed_qps++;
	return 0;
}
int
ibv_modify_qp(struct ibv_qp *q, struct ibv_qp_attr *a, int mask)
{
	(void)q;
	(void)mask;
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
	assert(wr->send_flags & IBV_SEND_SIGNALED);
	accepted[num_accepted++] = wr->wr_id;
	return 0;
}
int
ibv_post_recv(struct ibv_qp *q, struct ibv_recv_wr *wr, struct ibv_recv_wr **bad)
{
	(void)q;
	(void)bad;
	if (post_error)
		return post_error;
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
	assert(ops->deregister_memory(mr) == EBUSY);
	assert(ops->progress(ep, out, 2, &count) == 0 && count == 1);
	assert(out[0].error == ECANCELED);
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

int
main(void)
{
	test_lifetime();
	test_success_and_open_failure();
	test_slot_recycling();
	puts("native verbs ownership, backpressure, completion and cleanup tests passed");
	return 0;
}
