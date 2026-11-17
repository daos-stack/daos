/*
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#include "../rpc.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

struct result {
	unsigned count;
	int      error;
	uint32_t status;
	char     data[32];
};
static void
done(void *arg, int error, uint32_t status, const void *data, size_t size)
{
	struct result *r = arg;
	r->count++;
	r->error  = error;
	r->status = status;
	assert(size < sizeof(r->data));
	memset(r->data, 0, sizeof(r->data));
	memcpy(r->data, data, size);
}
static void
echo(void *arg, struct crt_native_rpc *rpc, struct crt_native_token token, uint32_t opcode,
     const void *data, size_t size)
{
	unsigned *calls = arg;
	(*calls)++;
	assert(opcode == 17);
	assert(crt_native_rpc_reply(rpc, token, 123, data, size) == 0);
	assert(crt_native_rpc_reply(rpc, token, 123, data, size) == ENOENT);
}

static void
pipeline(void)
{
	struct crt_native_rpc *a, *b;
	struct crt_native_tx   requests[4], response, ignored;
	struct result          results[4] = {0};
	unsigned               handled    = 0;
	uint64_t               ids[4], id;
	char                   stale[256];
	size_t                 stale_size = 0;
	void                  *slab;
	size_t                 slab_size;

	assert(crt_native_rpc_create(4, 256, echo, &handled, &a) == 0);
	assert(crt_native_rpc_create(4, 256, echo, &handled, &b) == 0);
	assert(crt_native_rpc_buffers(a, &slab, &slab_size) == 0 && slab != NULL &&
	       slab_size == 4 * 4 * 256);
	for (unsigned round = 0; round < 1024; round++) {
		for (unsigned i = 0; i < 4; i++) {
			assert(crt_native_rpc_call(a, 17, "hello", 5, done, &results[i], &ids[i]) ==
			       0);
			assert(crt_native_rpc_set_deadline(a, ids[i], 100 + i) == 0);
			assert(crt_native_rpc_next_tx(a, &requests[i]) == 0);
		}
		assert(crt_native_rpc_call(a, 17, NULL, 0, done, &results[0], &id) == EAGAIN);
		assert(crt_native_rpc_next_tx(a, &ignored) == EAGAIN);
		assert(crt_native_rpc_destroy(a) == EBUSY);
		/* Replies may arrive before the local SEND completion. They must not
		 * recycle a frame still used by DMA or trigger an early callback.
		 */
		for (unsigned i = 0; i < 4; i++) {
			assert(crt_native_rpc_receive(b, requests[i].data, requests[i].length) ==
			       0);
			assert(crt_native_rpc_next_tx(b, &response) == 0);
			if (round == 0 && i == 0) {
				stale_size = response.length;
				memcpy(stale, response.data, stale_size);
			}
			assert(crt_native_rpc_receive(a, response.data, response.length) == 0);
			assert(results[i].count == round);
			assert(crt_native_rpc_tx_complete(b, response.token, 0) == 0);
		}
		for (unsigned i = 4; i-- != 0;) {
			assert(crt_native_rpc_tx_complete(a, requests[i].token, 0) == 0);
			assert(results[i].count == round + 1 && results[i].error == 0 &&
			       results[i].status == 123 && strcmp(results[i].data, "hello") == 0);
			assert(crt_native_rpc_tx_complete(a, requests[i].token, 0) == ENOENT);
		}
		if (round != 0)
			assert(crt_native_rpc_receive(a, stale, stale_size) == 0);
		assert(crt_native_rpc_next_deadline(a) == 0);
	}
	assert(handled == 4096);
	assert(crt_native_rpc_destroy(a) == 0);
	assert(crt_native_rpc_destroy(b) == 0);
}

static void
cancellation(void)
{
	struct crt_native_rpc *rpc;
	struct crt_native_tx   tx;
	struct result          r       = {0};
	unsigned               handled = 0;
	uint64_t               id;

	assert(crt_native_rpc_create(1, 64, echo, &handled, &rpc) == 0);
	assert(crt_native_rpc_call(rpc, 17, "x", 1, done, &r, &id) == 0);
	assert(crt_native_rpc_cancel(rpc, id, ETIMEDOUT) == 0);
	assert(r.count == 1 && r.error == ETIMEDOUT);
	assert(crt_native_rpc_next_tx(rpc, &tx) == EAGAIN);
	assert(crt_native_rpc_call(rpc, 17, "x", 1, done, &r, &id) == 0);
	assert(crt_native_rpc_next_tx(rpc, &tx) == 0);
	assert(crt_native_rpc_cancel(rpc, id, ECANCELED) == 0);
	assert(r.count == 1);
	assert(crt_native_rpc_destroy(rpc) == EBUSY);
	assert(crt_native_rpc_tx_complete(rpc, tx.token, 0) == 0);
	assert(r.count == 2 && r.error == ECANCELED);
	assert(crt_native_rpc_call(rpc, 17, "x", 1, done, &r, &id) == 0);
	assert(crt_native_rpc_next_tx(rpc, &tx) == 0);
	assert(crt_native_rpc_fail(rpc, ECONNRESET) == 0);
	assert(crt_native_rpc_destroy(rpc) == EBUSY && r.count == 2);
	assert(crt_native_rpc_tx_complete(rpc, tx.token, ECANCELED) == 0);
	assert(r.count == 3 && r.error == ECONNRESET);
	assert(crt_native_rpc_call(rpc, 17, "x", 1, done, &r, &id) == ECONNRESET);
	assert(crt_native_rpc_destroy(rpc) == 0);
}

static void
reply_before_request_and_bad_frame(void)
{
	struct crt_native_rpc   *rpc;
	struct result            r       = {0};
	unsigned                 handled = 0;
	uint64_t                 id;
	char                     frame[64];
	struct crt_native_header h = {.type = CRT_NATIVE_REPLY, .opcode = 17};

	assert(crt_native_rpc_create(1, 64, echo, &handled, &rpc) == 0);
	assert(crt_native_rpc_call(rpc, 17, NULL, 0, done, &r, &id) == 0);
	h.request_id = id;
	assert(crt_native_encode(&h, frame, sizeof(frame)) == 0);
	assert(crt_native_rpc_receive(rpc, frame, CRT_NATIVE_HEADER_SIZE) == EPROTO);
	assert(r.count == 0);
	assert(crt_native_rpc_fail(rpc, EPROTO) == 0);
	assert(r.count == 1 && r.error == EPROTO);
	assert(crt_native_rpc_destroy(rpc) == 0);
}

static int
released(void *arg, uint64_t id)
{
	uint64_t *last = arg;
	*last          = id;
	return 0;
}

static void
release_queue(void)
{
	struct crt_native_rpc *rpc, *peer;
	unsigned               handled = 0;
	struct result          result  = {0};
	struct crt_native_tx   tx, call;
	uint64_t               id, last = 0;
	assert(crt_native_rpc_create(1, 64, echo, &handled, &rpc) == 0);
	assert(crt_native_rpc_create(1, 64, echo, &handled, &peer) == 0);
	assert(crt_native_rpc_set_release_handler(peer, released, &last) == 0);
	assert(crt_native_rpc_call(rpc, 17, NULL, 0, done, &result, &id) == 0);
	assert(crt_native_rpc_next_tx(rpc, &call) == 0);
	/* Key ACK does not consume a request slot, even with the only call busy. */
	assert(crt_native_rpc_queue_release(rpc, UINT64_C(0x1234567800000007)) == 0);
	assert(crt_native_rpc_queue_release(rpc, 42) == EAGAIN);
	assert(crt_native_rpc_next_tx(rpc, &tx) == 0 && tx.token.control);
	assert(crt_native_rpc_receive(peer, tx.data, tx.length) == 0);
	assert(last == UINT64_C(0x1234567800000007) && handled == 0);
	assert(crt_native_rpc_tx_complete(rpc, tx.token, 0) == 0);
	assert(crt_native_rpc_tx_complete(rpc, tx.token, 0) == ENOENT);
	assert(crt_native_rpc_queue_release(rpc, 9) == 0);
	assert(crt_native_rpc_fail(rpc, ECANCELED) == 0); /* queued controls released */
	assert(crt_native_rpc_tx_complete(rpc, call.token, ECANCELED) == 0);
	assert(result.count == 1 && result.error == ECANCELED);
	assert(crt_native_rpc_destroy(rpc) == 0);
	assert(crt_native_rpc_destroy(peer) == 0);
}

static void
deadlines(void)
{
	struct crt_native_rpc *rpc;
	struct result          results[128] = {0};
	uint64_t               ids[128], times[128];
	unsigned               handled = 0, expired;
	assert(crt_native_rpc_create(128, 64, echo, &handled, &rpc) == 0);
	for (unsigned i = 0; i < 128; i++) {
		assert(crt_native_rpc_call(rpc, 17, NULL, 0, done, &results[i], &ids[i]) == 0);
		times[i] = 1000 + (i * 53) % 128;
		assert(crt_native_rpc_set_deadline(rpc, ids[i], times[i]) == 0);
	}
	for (unsigned i = 0; i < 128; i++) {
		if (i % 3 == 0)
			times[i] += 5000;
		if (i % 5 == 0)
			times[i] = 0;
		if (i % 7 == 0)
			times[i] = 100 + i;
		assert(crt_native_rpc_set_deadline(rpc, ids[i], times[i]) == 0);
	}
	unsigned total = 0;
	while (crt_native_rpc_next_deadline(rpc) && crt_native_rpc_next_deadline(rpc) <= 2000) {
		assert(crt_native_rpc_expire(rpc, 2000, &expired) == 0 && expired > 0 &&
		       expired <= 32);
		total += expired;
	}
	unsigned expected = 0;
	for (unsigned i = 0; i < 128; i++) {
		if (times[i] && times[i] <= 2000) {
			expected++;
			assert(results[i].count == 1 && results[i].error == ETIMEDOUT);
			assert(crt_native_rpc_set_deadline(rpc, ids[i], 1) == ENOENT);
		} else {
			assert(results[i].count == 0);
			assert(crt_native_rpc_set_deadline(rpc, ids[i], 0) == 0);
		}
	}
	assert(total == expected && crt_native_rpc_next_deadline(rpc) == 0);
	/* More expired requests than one tick's budget must not leak into TX. */
	for (unsigned i = 0; i < 128; i++) {
		if (results[i].count == 0)
			assert(crt_native_rpc_set_deadline(rpc, ids[i], 3000) == 0);
	}
	assert(crt_native_rpc_expire(rpc, 3000, &expired) == 0 && expired <= 32);
	struct crt_native_tx blocked;
	assert(crt_native_rpc_next_tx(rpc, &blocked) == EAGAIN);
	assert(crt_native_rpc_fail(rpc, ECANCELED) == 0);
	for (unsigned i = 0; i < 128; i++)
		assert(results[i].count == 1);
	assert(crt_native_rpc_destroy(rpc) == 0);

	struct result        result = {0};
	struct crt_native_tx tx;
	uint64_t             id;
	assert(crt_native_rpc_create(1, 64, echo, &handled, &rpc) == 0);
	assert(crt_native_rpc_call(rpc, 17, NULL, 0, done, &result, &id) == 0);
	assert(crt_native_rpc_next_tx(rpc, &tx) == 0);
	assert(crt_native_rpc_set_deadline(rpc, id, 10) == 0);
	assert(crt_native_rpc_expire(rpc, 9, &expired) == 0 && expired == 0);
	assert(crt_native_rpc_expire(rpc, 10, &expired) == 0 && expired == 1);
	assert(crt_native_rpc_next_deadline(rpc) == 0 && result.count == 0);
	assert(crt_native_rpc_set_deadline(rpc, id, 20) == EALREADY);
	assert(crt_native_rpc_destroy(rpc) == EBUSY);
	assert(crt_native_rpc_tx_complete(rpc, tx.token, 0) == 0);
	assert(result.count == 1 && result.error == ETIMEDOUT);
	assert(crt_native_rpc_call(rpc, 17, NULL, 0, done, &result, &id) == 0);
	assert(crt_native_rpc_next_tx(rpc, &tx) == 0);
	assert(crt_native_rpc_tx_complete(rpc, tx.token, 0) == 0);
	assert(crt_native_rpc_set_deadline(rpc, id, 20) == 0);
	assert(crt_native_rpc_expire(rpc, 20, &expired) == 0 && expired == 1);
	assert(result.count == 2 && result.error == ETIMEDOUT);
	assert(crt_native_rpc_destroy(rpc) == 0);
}

struct ack_counts {
	unsigned count;
	uint64_t sum;
};
static int
count_ack(void *arg, uint64_t id)
{
	struct ack_counts *counts = arg;
	counts->count++;
	counts->sum += id;
	return 0;
}
static void
batched_and_piggybacked_releases(void)
{
	struct crt_native_rpc      *a, *b;
	struct crt_native_tx        tx, reply;
	unsigned                    handled = 0;
	struct result               result  = {0};
	struct ack_counts           ack_a = {0}, ack_b = {0};
	struct crt_native_header    header;
	struct crt_native_rpc_stats stats;
	uint64_t                    id;
	assert(crt_native_rpc_create(16, 256, echo, &handled, &a) == 0);
	assert(crt_native_rpc_create(16, 256, echo, &handled, &b) == 0);
	assert(crt_native_rpc_set_release_handler(a, count_ack, &ack_a) == 0);
	assert(crt_native_rpc_set_release_handler(b, count_ack, &ack_b) == 0);
	for (unsigned i = 1; i <= 16; i++)
		assert(crt_native_rpc_queue_release(a, i) == 0);
	assert(crt_native_rpc_queue_release(a, 17) == EAGAIN);
	assert(crt_native_rpc_next_tx(a, &tx) == 0 && tx.token.control);
	assert(crt_native_decode(tx.data, tx.length, &header) == 0 && header.release_count == 15);
	assert(crt_native_rpc_receive(b, tx.data, tx.length) == 0 && ack_b.count == 16 &&
	       ack_b.sum == 136);
	assert(crt_native_rpc_tx_complete(a, tx.token, 0) == 0);
	assert(crt_native_rpc_next_tx(a, &tx) == EAGAIN);
	for (unsigned i = 1; i <= 8; i++)
		assert(crt_native_rpc_queue_release(a, i) == 0);
	assert(crt_native_rpc_call(a, 17, "hello", 5, done, &result, &id) == 0);
	assert(crt_native_rpc_next_tx(a, &tx) == 0 && !tx.token.control);
	assert(crt_native_decode(tx.data, tx.length, &header) == 0 && header.release_count == 8);
	assert(crt_native_rpc_receive(b, tx.data, tx.length) == 0 && ack_b.count == 24 &&
	       ack_b.sum == 172);
	assert(crt_native_rpc_tx_complete(a, tx.token, 0) == 0);
	assert(crt_native_rpc_queue_release(b, 100) == 0);
	assert(crt_native_rpc_next_tx(b, &reply) == 0 && reply.token.reply);
	assert(crt_native_decode(reply.data, reply.length, &header) == 0 &&
	       header.release_count == 1);
	assert(crt_native_rpc_receive(a, reply.data, reply.length) == 0 && ack_a.count == 1);
	assert(result.count == 1 && strcmp(result.data, "hello") == 0);
	assert(crt_native_rpc_tx_complete(b, reply.token, 0) == 0);
	/* Cancel an unposted carrier while all ACK reservations are occupied. */
	for (unsigned i = 1; i <= 16; i++)
		assert(crt_native_rpc_queue_release(a, i) == 0);
	assert(crt_native_rpc_call(a, 17, NULL, 0, done, &result, &id) == 0);
	assert(crt_native_rpc_next_tx(a, &tx) == 0 && !tx.token.control);
	assert(crt_native_rpc_queue_release(a, 99) == EAGAIN);
	assert(crt_native_rpc_cancel(a, id, ECANCELED) == 0);
	assert(crt_native_rpc_tx_complete(a, tx.token, ECANCELED) == 0);
	assert(crt_native_rpc_queue_release(a, 99) == EAGAIN);
	assert(crt_native_rpc_next_tx(a, &tx) == 0 && tx.token.control);
	assert(crt_native_rpc_receive(b, tx.data, tx.length) == 0 && ack_b.count == 40 &&
	       ack_b.sum == 308);
	assert(crt_native_rpc_tx_complete(a, tx.token, 0) == 0);
	crt_native_rpc_get_stats(a, &stats);
	assert(stats.release_frames == 2 && stats.piggyback_ids == 24);
	assert(crt_native_rpc_destroy(a) == 0 && crt_native_rpc_destroy(b) == 0);
	/* No footer space: flush the ACK independently before the full RPC. */
	assert(crt_native_rpc_create(2, 64, echo, &handled, &a) == 0);
	assert(crt_native_rpc_create(2, 64, echo, &handled, &b) == 0);
	assert(crt_native_rpc_set_release_handler(b, count_ack, &ack_b) == 0);
	char payload[31];
	memset(payload, 'x', sizeof(payload));
	assert(crt_native_rpc_call(a, 17, payload, sizeof(payload), done, &result, &id) == 0);
	assert(crt_native_rpc_queue_release(a, 200) == 0);
	assert(crt_native_rpc_next_tx(a, &tx) == 0 && tx.token.control);
	assert(crt_native_rpc_receive(b, tx.data, tx.length) == 0);
	assert(crt_native_rpc_tx_complete(a, tx.token, 0) == 0);
	assert(crt_native_rpc_next_tx(a, &tx) == 0 && !tx.token.control);
	assert(crt_native_rpc_receive(b, tx.data, tx.length) == 0);
	assert(crt_native_rpc_tx_complete(a, tx.token, 0) == 0);
	assert(crt_native_rpc_next_tx(b, &reply) == 0);
	assert(crt_native_rpc_receive(a, reply.data, reply.length) == 0);
	assert(crt_native_rpc_tx_complete(b, reply.token, 0) == 0);
	assert(crt_native_rpc_destroy(a) == 0 && crt_native_rpc_destroy(b) == 0);
}

int
main(void)
{
	deadlines();
	release_queue();
	batched_and_piggybacked_releases();
	pipeline();
	cancellation();
	reply_before_request_and_bad_frame();
	puts("RPC engine: pipelining, matching, generations, cancellation and pinned-send tests "
	     "passed");
	return 0;
}
