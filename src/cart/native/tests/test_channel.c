/*
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#include "../channel.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define QUEUE 256
struct crt_native_mr {
	uint8_t *base;
	size_t   size;
	unsigned refs;
};
struct queued {
	struct crt_native_completion completion;
	struct crt_native_mr        *mr;
};
struct receive {
	struct crt_native_mr *mr;
	size_t                offset, size;
	void                 *cookie;
};
struct crt_native_ep {
	struct crt_native_ep *peer;
	struct queued         cq[QUEUE];
	struct receive        rx[QUEUE];
	unsigned              cq_head, cq_tail, rx_head, rx_tail;
	unsigned              send_block, recv_block;
	bool                  closing;
};
static unsigned
capacity(struct crt_native_ep *ep)
{
	(void)ep;
	return 64;
}
static int
reg(struct crt_native_ep *ep, void *base, size_t size, unsigned remote, struct crt_native_mr **out,
    struct crt_native_remote_memory *desc)
{
	(void)ep;
	(void)desc;
	assert(!remote);
	*out = calloc(1, sizeof(**out));
	assert(*out);
	(*out)->base = base;
	(*out)->size = size;
	return 0;
}
static int
dereg(struct crt_native_mr *mr)
{
	assert(mr->refs == 0);
	free(mr);
	return 0;
}
static void
complete(struct crt_native_ep *ep, struct crt_native_mr *mr, enum crt_native_opcode op,
	 void *cookie, size_t bytes, int error)
{
	assert(ep->cq_tail - ep->cq_head < QUEUE);
	ep->cq[ep->cq_tail++ % QUEUE] = (struct queued){
	    .completion = {.opcode = op, .cookie = cookie, .bytes = bytes, .error = error},
	    .mr         = mr,
	};
}
static int
post(struct crt_native_ep *ep, enum crt_native_opcode op, struct crt_native_mr *mr, size_t offset,
     size_t size, const struct crt_native_remote_memory *remote, size_t remote_offset, void *cookie)
{
	(void)remote;
	(void)remote_offset;
	assert(offset <= mr->size && size <= mr->size - offset);
	if (ep->closing)
		return ESHUTDOWN;
	if (op == CRT_NATIVE_RECV) {
		if (ep->recv_block) {
			ep->recv_block--;
			return EAGAIN;
		}
		assert(ep->rx_tail - ep->rx_head < QUEUE);
		ep->rx[ep->rx_tail++ % QUEUE] = (struct receive){mr, offset, size, cookie};
		mr->refs++;
		return 0;
	}
	assert(op == CRT_NATIVE_SEND);
	if (ep->send_block) {
		ep->send_block--;
		return EAGAIN;
	}
	struct crt_native_ep *peer = ep->peer;
	if (peer->rx_head == peer->rx_tail)
		return EAGAIN;
	struct receive rx = peer->rx[peer->rx_head++ % QUEUE];
	assert(size <= rx.size);
	memcpy(rx.mr->base + rx.offset, mr->base + offset, size);
	complete(peer, rx.mr, CRT_NATIVE_RECV, rx.cookie, size, 0);
	mr->refs++;
	complete(ep, mr, CRT_NATIVE_SEND, cookie, size, 0);
	return 0;
}
static int
progress(struct crt_native_ep *ep, struct crt_native_completion *out, unsigned capacity,
	 unsigned *count)
{
	*count = 0;
	while (ep->cq_head != ep->cq_tail && *count < capacity) {
		struct queued q = ep->cq[ep->cq_head++ % QUEUE];
		assert(q.mr->refs);
		q.mr->refs--;
		out[(*count)++] = q.completion;
	}
	return 0;
}
static int
close_begin(struct crt_native_ep *ep)
{
	ep->closing = true;
	while (ep->rx_head != ep->rx_tail) {
		struct receive rx = ep->rx[ep->rx_head++ % QUEUE];
		complete(ep, rx.mr, CRT_NATIVE_RECV, rx.cookie, 0, ECANCELED);
	}
	return 0;
}
static const struct crt_native_ops ops = {
    .capacity          = capacity,
    .register_memory   = reg,
    .deregister_memory = dereg,
    .post              = post,
    .progress          = progress,
    .close_begin       = close_begin,
};
static void
echo(void *arg, struct crt_native_rpc *rpc, struct crt_native_token token, uint32_t opcode,
     const void *payload, size_t size)
{
	unsigned *handled = arg;
	(*handled)++;
	assert(opcode == 17);
	assert(crt_native_rpc_reply(rpc, token, 0, payload, size) == 0);
}
struct result {
	unsigned done, failed;
};
static void
done(void *arg, int error, uint32_t status, const void *payload, size_t size)
{
	struct result *r = arg;
	r->done++;
	if (error)
		r->failed++;
	else
		assert(status == 0 && size == 5 && memcmp(payload, "hello", 5) == 0);
}
static void
drain(struct crt_native_channel *c)
{
	unsigned completed;
	assert(crt_native_channel_shutdown(c, ECANCELED) == 0);
	for (unsigned i = 0; i < 128; i++) {
		int rc = crt_native_channel_destroy(c);
		if (rc == 0)
			return;
		assert(rc == EBUSY);
		assert(crt_native_channel_progress(c, &completed) == ECANCELED);
	}
	assert(!"channel failed to drain");
}
int
main(void)
{
	struct crt_native_ep       a = {0}, b = {0};
	struct crt_native_channel *ca, *cb;
	struct crt_native_limits   limits = {.frame_size = 128, .receives = 8};
	struct result              ra = {0}, rb = {0};
	unsigned                   handled = 0, completed;
	uint64_t                   id;
	a.peer = &b;
	b.peer = &a;
	assert(crt_native_channel_create(&ops, &a, &limits, echo, &handled, &ca) == 0);
	assert(crt_native_channel_create(&ops, &b, &limits, echo, &handled, &cb) == 0);
	assert(crt_native_channel_start(ca) == 0);
	assert(crt_native_channel_start(cb) == 0);
	assert(crt_native_channel_destroy(ca) == EBUSY);
	/* Simultaneous full request windows, including transient SEND/RECV
	 * posting backpressure. Replenishing receives must not invoke a handler
	 * twice when only its repost failed.
	 */
	for (unsigned round = 0; round < 1024; round++) {
		a.send_block = 2;
		b.recv_block = 2;
		for (unsigned i = 0; i < 4; i++) {
			assert(crt_native_channel_call(ca, 17, "hello", 5, done, &ra, &id) == 0);
			assert(crt_native_channel_call(cb, 17, "hello", 5, done, &rb, &id) == 0);
		}
		unsigned target = (round + 1) * 4;
		for (unsigned i = 0; i < 128 && (ra.done < target || rb.done < target); i++) {
			assert(crt_native_channel_progress(ca, &completed) == 0);
			assert(crt_native_channel_progress(cb, &completed) == 0);
		}
		assert(ra.done == target && rb.done == target && ra.failed == 0 && rb.failed == 0);
	}
	assert(handled == 8192);
	/* Shutdown an engine-owned frame which has not yet been accepted by the
	 * transport. Its callback must complete without waiting for a nonexistent CQE.
	 */
	a.send_block = 1;
	assert(crt_native_channel_call(ca, 17, "hello", 5, done, &ra, &id) == 0);
	assert(crt_native_channel_progress(ca, &completed) == 0);
	drain(ca);
	assert(ra.done == 4097 && ra.failed == 1);
	drain(cb);
	puts(
	    "channel: full duplex RPCs, pooled I/O, retry, receive replenishment and drain passed");
	return 0;
}
