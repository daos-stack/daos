/*
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "channel.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <limits.h>
#include <time.h>

enum io_state { IDLE, POSTED, HELD, PINNED };
struct channel_io {
	bool                    receive;
	bool                    delivered;
	enum io_state           state;
	unsigned                index;
	size_t                  size;
	struct crt_native_token token;
	atomic_uint             refs;
	uint64_t                generation;
	unsigned                released_next;
};
struct crt_native_channel {
	const struct crt_native_ops *ops;
	struct crt_native_ep        *ep;
	struct crt_native_rpc       *rpc;
	struct crt_native_mr        *rx_mr, *tx_mr;
	uint8_t                     *rx_buffer, *tx_buffer;
	struct channel_io           *rx, *tx;
	size_t                       frame_size;
	unsigned                     count, outstanding;
	unsigned                     rx_pending_head, rx_pending_tail;
	unsigned                    *rx_next;
	unsigned                     tx_free_head;
	unsigned                    *tx_next;
	struct crt_native_tx         waiting;
	bool                         has_waiting, started, running;
	int                          error;
	struct channel_io           *delivering;
	atomic_uint                  leases, released_head;
	unsigned                     released_pending;
};

static int
stop(struct crt_native_channel *c, int error)
{
	int rc;
	if (c->error == 0) {
		c->error = error;
		crt_native_rpc_fail(c->rpc, error);
		if (c->has_waiting) {
			crt_native_rpc_tx_complete(c->rpc, c->waiting.token, error);
			c->has_waiting = false;
		}
	}
	/* Retry a failed QP error transition on subsequent shutdown calls. */
	rc = c->ops->close_begin(c->ep);
	return rc != 0 ? rc : c->error;
}

int
crt_native_channel_create(const struct crt_native_ops *ops, struct crt_native_ep *ep,
			  const struct crt_native_limits *limits, crt_native_handler_t handler,
			  void *arg, struct crt_native_channel **output)
{
	struct crt_native_channel      *c;
	struct crt_native_remote_memory ignored;
	size_t                          tx_size;
	int                             rc;

	if (output == NULL)
		return EINVAL;
	*output = NULL;
	if (ops == NULL || ep == NULL || limits == NULL || ops->capacity == NULL ||
	    ops->post == NULL || ops->progress == NULL || ops->register_memory == NULL ||
	    ops->deregister_memory == NULL || ops->close_begin == NULL || limits->receives < 2 ||
	    limits->receives > 32768 || limits->frame_size < 64 || limits->frame_size > 1048576 ||
	    ops->capacity(ep) < 2 * limits->receives ||
	    limits->frame_size > SIZE_MAX / limits->receives)
		return EINVAL;
	c = calloc(1, sizeof(*c));
	if (c == NULL)
		return ENOMEM;
	c->ops             = ops;
	c->ep              = ep;
	c->count           = limits->receives;
	c->frame_size      = limits->frame_size;
	c->rx_pending_head = c->rx_pending_tail = c->count;
	atomic_init(&c->leases, 0);
	atomic_init(&c->released_head, c->count);
	c->released_pending = c->count;
	c->rx               = calloc(c->count, sizeof(*c->rx));
	c->tx               = calloc(c->count, sizeof(*c->tx));
	c->rx_next          = calloc(c->count, sizeof(*c->rx_next));
	c->tx_next          = calloc(c->count, sizeof(*c->tx_next));
	c->rx_buffer        = calloc(c->count, c->frame_size);
	if (!c->rx || !c->tx || !c->rx_next || !c->tx_next || !c->rx_buffer) {
		rc = ENOMEM;
		goto fail;
	}
	for (unsigned i = 0; i < c->count; i++) {
		c->rx[i] = (struct channel_io){.receive = true, .index = i};
		atomic_init(&c->rx[i].refs, 0);
		c->tx[i].index = i;
		c->tx_next[i]  = i + 1;
	}
	rc = crt_native_rpc_create(c->count / 2, c->frame_size, handler, arg, &c->rpc);
	if (rc != 0)
		goto fail;
	void *base;
	crt_native_rpc_buffers(c->rpc, &base, &tx_size);
	c->tx_buffer = base;
	rc = ops->register_memory(ep, c->rx_buffer, c->count * c->frame_size, false, &c->rx_mr,
				  &ignored);
	if (rc != 0)
		goto fail;
	rc = ops->register_memory(ep, c->tx_buffer, tx_size, false, &c->tx_mr, &ignored);
	if (rc != 0)
		goto fail;
	*output = c;
	return 0;
fail:
	if (c->rx_mr && ops->deregister_memory(c->rx_mr) != 0) {
		c->error = rc;
		*output  = c;
		return rc;
	}
	if (c->rpc)
		crt_native_rpc_destroy(c->rpc);
	free(c->rx_buffer);
	free(c->rx_next);
	free(c->tx_next);
	free(c->rx);
	free(c->tx);
	free(c);
	return rc;
}

static int
post_receive(struct crt_native_channel *c, struct channel_io *io)
{
	int rc = c->ops->post(c->ep, CRT_NATIVE_RECV, c->rx_mr, io->index * c->frame_size,
			      c->frame_size, NULL, 0, io);
	if (rc == 0) {
		io->state = POSTED;
		c->outstanding++;
	}
	return rc;
}

int
crt_native_channel_start(struct crt_native_channel *c)
{
	if (c == NULL || c->running)
		return EINVAL;
	if (c->error)
		return c->error;
	for (unsigned i = 0; i < c->count; i++) {
		int rc;
		if (c->rx[i].state != IDLE)
			continue;
		rc = post_receive(c, &c->rx[i]);
		if (rc == EAGAIN)
			return rc;
		if (rc != 0)
			return stop(c, rc);
	}
	c->started = true;
	return 0;
}

int
crt_native_channel_call(struct crt_native_channel *c, uint32_t opcode, const void *data,
			size_t size, crt_native_done_t done, void *arg, uint64_t *id)
{
	if (c == NULL)
		return EINVAL;
	if (c->error)
		return c->error;
	if (!c->started)
		return ENOTCONN;
	return crt_native_rpc_call(c->rpc, opcode, data, size, done, arg, id);
}

int
crt_native_channel_call_encoded(struct crt_native_channel *c, uint32_t opcode,
				crt_native_encoder_t encode, void *encode_arg,
				crt_native_done_t done, void *arg, uint64_t *id)
{
	if (c == NULL)
		return EINVAL;
	if (c->error)
		return c->error;
	if (!c->started)
		return ENOTCONN;
	return crt_native_rpc_call_encoded(c->rpc, opcode, encode, encode_arg, done, arg, id);
}

int
crt_native_channel_set_release_handler(struct crt_native_channel *c, crt_native_release_t release,
				       void *arg)
{
	if (c == NULL || c->running)
		return EINVAL;
	if (c->error)
		return c->error;
	return crt_native_rpc_set_release_handler(c->rpc, release, arg);
}

int
crt_native_channel_queue_release(struct crt_native_channel *c, uint64_t id)
{
	if (c == NULL)
		return EINVAL;
	if (c->error)
		return c->error;
	if (!c->started)
		return ENOTCONN;
	return crt_native_rpc_queue_release(c->rpc, id);
}

int
crt_native_channel_set_deadline(struct crt_native_channel *c, uint64_t id, uint64_t deadline)
{
	return c == NULL ? EINVAL : crt_native_rpc_set_deadline(c->rpc, id, deadline);
}

int
crt_native_channel_cancel(struct crt_native_channel *c, uint64_t id, int error)
{
	return c == NULL ? EINVAL : crt_native_rpc_cancel(c->rpc, id, error);
}

uint64_t
crt_native_channel_next_deadline(const struct crt_native_channel *c)
{
	return c == NULL ? 0 : crt_native_rpc_next_deadline(c->rpc);
}

int
crt_native_channel_retain(struct crt_native_channel *c, const void *view, size_t size,
			  struct crt_native_rx_lease *lease)
{
	if (c == NULL || view == NULL || lease == NULL || lease->channel != NULL ||
	    c->delivering == NULL)
		return EINVAL;
	struct channel_io *io      = c->delivering;
	uintptr_t          base    = (uintptr_t)(c->rx_buffer + io->index * c->frame_size);
	uintptr_t          address = (uintptr_t)view;
	if (address < base || address - base > io->size || size > io->size - (address - base))
		return EINVAL;
	if (atomic_load(&io->refs) == UINT_MAX || atomic_load(&c->leases) == UINT_MAX)
		return EOVERFLOW;
	atomic_fetch_add(&io->refs, 1);
	atomic_fetch_add(&c->leases, 1);
	*lease = (struct crt_native_rx_lease){c, io->index, io->generation};
	return 0;
}

int
crt_native_channel_release(struct crt_native_rx_lease *lease)
{
	if (lease == NULL || lease->channel == NULL)
		return EINVAL;
	struct crt_native_channel *c = lease->channel;
	if (lease->index >= c->count)
		return EINVAL;
	struct channel_io *io = &c->rx[lease->index];
	if (io->generation != lease->generation || atomic_load(&io->refs) == 0)
		return EINVAL;
	if (atomic_fetch_sub(&io->refs, 1) == 1) {
		unsigned head = atomic_load(&c->released_head);
		do {
			io->released_next = head;
		} while (!atomic_compare_exchange_weak(&c->released_head, &head, io->index));
	}
	*lease = (struct crt_native_rx_lease){0};
	/* Last operation touching c: destroy may run after this decrement. */
	atomic_fetch_sub(&c->leases, 1);
	return 0;
}

static void
hold_receive(struct crt_native_channel *c, struct channel_io *io)
{
	io->state             = HELD;
	c->rx_next[io->index] = c->count;
	if (c->rx_pending_tail != c->count)
		c->rx_next[c->rx_pending_tail] = io->index;
	else
		c->rx_pending_head = io->index;
	c->rx_pending_tail = io->index;
}

static int
handle_receive(struct crt_native_channel *c, struct channel_io *io)
{
	int rc;
	io->state = HELD;
	if (!io->delivered) {
		if (io->generation == UINT64_MAX)
			return EOVERFLOW;
		io->generation++;
		atomic_store(&io->refs, 1); /* progress owner's borrow */
		c->delivering = io;
		rc = crt_native_rpc_receive(c->rpc, c->rx_buffer + io->index * c->frame_size,
					    io->size);
		c->delivering = NULL;
		bool pinned   = atomic_fetch_sub(&io->refs, 1) > 1;
		if (pinned)
			io->state = PINNED;
		if (rc != 0)
			return rc;
		io->delivered = true;
		if (pinned)
			return 0;
	}
	/* Mark processed before reposting; EAGAIN must not replay the handler. */
	rc = post_receive(c, io);
	if (rc == 0)
		io->delivered = false;
	return rc;
}

static int
send_queued(struct crt_native_channel *c)
{
	for (unsigned budget = 0; budget < 32 && c->tx_free_head != c->count; budget++) {
		unsigned           slot = c->tx_free_head;
		struct channel_io *io   = &c->tx[slot];
		int                rc;

		if (!c->has_waiting) {
			rc = crt_native_rpc_next_tx(c->rpc, &c->waiting);
			if (rc == EAGAIN)
				return 0;
			if (rc != 0)
				return rc;
			c->has_waiting = true;
		}
		rc = crt_native_rpc_tx_status(c->rpc, c->waiting.token);
		if (rc != 0) {
			int completion_rc =
			    crt_native_rpc_tx_complete(c->rpc, c->waiting.token, rc);
			c->has_waiting = false;
			if (completion_rc != 0)
				return completion_rc;
			continue;
		}
		rc = c->ops->post(c->ep, CRT_NATIVE_SEND, c->tx_mr,
				  (const uint8_t *)c->waiting.data - c->tx_buffer,
				  c->waiting.length, NULL, 0, io);
		if (rc == EAGAIN)
			return 0;
		if (rc != 0)
			return rc;
		io->token       = c->waiting.token;
		io->state       = POSTED;
		c->tx_free_head = c->tx_next[slot];
		c->outstanding++;
		c->has_waiting = false;
	}
	return 0;
}

int
crt_native_channel_progress(struct crt_native_channel *c, unsigned *completed)
{
	struct crt_native_completion batch[32];
	unsigned                     count = 0;
	int                          rc;

	if (c == NULL || completed == NULL || c->running)
		return EINVAL;
	*completed = 0;
	c->running = true;
	if (c->error == 0 && crt_native_rpc_next_deadline(c->rpc) != 0) {
		struct timespec now;
		unsigned        expired;
		if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
			stop(c, EIO);
		else {
			rc = crt_native_rpc_expire(
			    c->rpc, (uint64_t)now.tv_sec * UINT64_C(1000000000) + now.tv_nsec,
			    &expired);
			if (rc != 0)
				stop(c, rc);
		}
	}
	rc = c->ops->progress(c->ep, batch, 32, &count);
	if (rc != 0)
		stop(c, rc);
	/* Process TX completions first to release reply records before RX frames
	 * from subsequent generations use the same bounded slots.
	 */
	for (unsigned pass = 0; pass < 2; pass++) {
		for (unsigned i = 0; i < count; i++) {
			struct channel_io *io = batch[i].cookie;
			if (io->receive != (pass == 1))
				continue;
			io->state = IDLE;
			c->outstanding--;
			if (batch[i].error != 0 && c->error == 0)
				stop(c, batch[i].error);
			if (!io->receive) {
				crt_native_rpc_tx_complete(c->rpc, io->token,
							   c->error ? c->error : batch[i].error);
				c->tx_next[io->index] = c->tx_free_head;
				c->tx_free_head       = io->index;
			} else if (c->error == 0) {
				io->size = batch[i].bytes;
				rc       = handle_receive(c, io);
				if (rc == EAGAIN)
					hold_receive(c, io);
				else if (rc != 0 && rc != EAGAIN)
					stop(c, rc);
			}
		}
	}
	*completed = count;
	/* Last lease release queues only an index, never touches the QP. Drain a
	 * bounded number on the progress owner, including during error teardown.
	 */
	for (unsigned budget = 0; budget < 32; budget++) {
		if (c->released_pending == c->count)
			c->released_pending = atomic_exchange(&c->released_head, c->count);
		if (c->released_pending == c->count)
			break;
		struct channel_io *io = &c->rx[c->released_pending];
		c->released_pending   = io->released_next;
		if (c->error == 0)
			hold_receive(c, io);
		else
			io->state = IDLE;
	}
	/* Retry held inbound frames in bounded round-robin order. */
	for (unsigned budget = 0; budget < 32 && c->error == 0 && c->rx_pending_head != c->count;
	     budget++) {
		unsigned           slot = c->rx_pending_head;
		struct channel_io *io   = &c->rx[slot];
		c->rx_pending_head      = c->rx_next[slot];
		if (c->rx_pending_head == c->count)
			c->rx_pending_tail = c->count;
		rc = handle_receive(c, io);
		if (rc == EAGAIN)
			hold_receive(c, io);
		else if (rc != 0 && rc != EAGAIN)
			stop(c, rc);
	}
	if (c->error == 0 && c->started) {
		rc = send_queued(c);
		if (rc != 0)
			stop(c, rc);
	}
	c->running = false;
	return c->error;
}

int
crt_native_channel_shutdown(struct crt_native_channel *c, int error)
{
	if (c == NULL || error <= 0 || c->running)
		return EINVAL;
	stop(c, error);
	return 0;
}

int
crt_native_channel_destroy(struct crt_native_channel *c)
{
	int rc;
	if (c == NULL)
		return EINVAL;
	if (c->running || c->outstanding != 0 || c->has_waiting || atomic_load(&c->leases))
		return EBUSY;
	if (!crt_native_rpc_idle(c->rpc))
		return EBUSY;
	if (c->rx_mr != NULL) {
		rc = c->ops->deregister_memory(c->rx_mr);
		if (rc != 0)
			return rc;
		c->rx_mr = NULL;
	}
	if (c->tx_mr != NULL) {
		rc = c->ops->deregister_memory(c->tx_mr);
		if (rc != 0)
			return rc;
		c->tx_mr = NULL;
	}
	if (c->rpc != NULL) {
		rc = crt_native_rpc_destroy(c->rpc);
		if (rc != 0)
			return rc;
	}
	free(c->rx_buffer);
	free(c->rx_next);
	free(c->tx_next);
	free(c->rx);
	free(c->tx);
	free(c);
	return 0;
}
