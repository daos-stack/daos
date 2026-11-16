/*
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#include "rpc.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

enum state { FREE, HANDLING, QUEUED, IN_FLIGHT, WAIT_REPLY };
struct entry {
	enum state        state;
	unsigned          next;
	uint64_t          id;
	uint32_t          generation, opcode, status;
	size_t            size, response_size;
	int               error;
	bool              received;
	crt_native_done_t done;
	void             *arg;
	uint8_t          *frame, *response;
};
struct crt_native_rpc {
	unsigned             depth, free_head, request_head, request_tail, reply_head, reply_tail;
	unsigned             active_calls, active_servers, driver_owned;
	size_t               frame_size;
	int                  failed;
	struct entry        *requests, *replies;
	uint8_t             *buffers;
	crt_native_handler_t handler;
	void                *handler_arg;
	bool                 callback;
};

static void
enqueue(struct entry *entries, unsigned *head, unsigned *tail, unsigned slot, unsigned end)
{
	entries[slot].next = end;
	if (*tail != end)
		entries[*tail].next = slot;
	else
		*head = slot;
	*tail = slot;
}

static unsigned
dequeue(struct entry *entries, unsigned *head, unsigned *tail, unsigned end)
{
	unsigned slot = *head;
	if (slot == end)
		return end;
	*head = entries[slot].next;
	if (*head == end)
		*tail = end;
	return slot;
}

static int
slot_of(struct crt_native_rpc *rpc, uint64_t id, unsigned *slot)
{
	uint32_t low = (uint32_t)id;
	if (low == 0 || low > rpc->depth || (id >> 32) == 0)
		return EINVAL;
	*slot = low - 1;
	return 0;
}

static void
finish(struct crt_native_rpc *rpc, unsigned slot)
{
	struct entry     *e        = &rpc->requests[slot];
	crt_native_done_t done     = e->done;
	void             *arg      = e->arg;
	int               error    = e->error;
	uint32_t          status   = e->status;
	size_t            size     = error ? 0 : e->response_size;
	const void       *response = e->response;

	/* Keep this slot unavailable until its callback returns, so a reentrant
	 * call cannot overwrite the borrowed response while it is being consumed.
	 */
	rpc->callback = true;
	done(arg, error, status, response, size);
	rpc->callback  = false;
	e->state       = FREE;
	e->next        = rpc->free_head;
	rpc->free_head = slot;
	rpc->active_calls--;
}

int
crt_native_rpc_create(unsigned depth, size_t frame_size, crt_native_handler_t handler, void *arg,
		      struct crt_native_rpc **output)
{
	struct crt_native_rpc *rpc;

	if (output == NULL)
		return EINVAL;
	*output = NULL;
	if (depth == 0 || depth > 16384 || frame_size < CRT_NATIVE_HEADER_SIZE ||
	    frame_size > 1048576 || handler == NULL || frame_size > SIZE_MAX / (3 * depth))
		return EINVAL;
	rpc = calloc(1, sizeof(*rpc));
	if (rpc == NULL)
		return ENOMEM;
	rpc->requests = calloc(depth, sizeof(*rpc->requests));
	rpc->replies  = calloc(depth, sizeof(*rpc->replies));
	rpc->buffers  = calloc(3 * depth, frame_size);
	if (rpc->requests == NULL || rpc->replies == NULL || rpc->buffers == NULL) {
		free(rpc->buffers);
		free(rpc->replies);
		free(rpc->requests);
		free(rpc);
		return ENOMEM;
	}
	rpc->depth        = depth;
	rpc->frame_size   = frame_size;
	rpc->handler      = handler;
	rpc->handler_arg  = arg;
	rpc->request_head = rpc->request_tail = depth;
	rpc->reply_head = rpc->reply_tail = depth;
	for (unsigned i = 0; i < depth; i++) {
		rpc->requests[i].next     = i + 1;
		rpc->requests[i].frame    = rpc->buffers + i * frame_size;
		rpc->requests[i].response = rpc->buffers + (depth + i) * frame_size;
		rpc->replies[i].frame     = rpc->buffers + (2 * depth + i) * frame_size;
	}
	*output = rpc;
	return 0;
}

static int
encode_frame(struct crt_native_rpc *rpc, struct entry *e, bool reply, const void *data, size_t size)
{
	struct crt_native_header h = {
	    .type         = reply ? CRT_NATIVE_REPLY : CRT_NATIVE_REQUEST,
	    .request_id   = e->id,
	    .opcode       = e->opcode,
	    .payload_size = (uint32_t)size,
	    .status       = e->status,
	};
	int rc = crt_native_encode(&h, e->frame, rpc->frame_size);
	if (rc != 0)
		return rc;
	if (size != 0)
		memcpy(e->frame + CRT_NATIVE_HEADER_SIZE, data, size);
	e->size = CRT_NATIVE_HEADER_SIZE + size;
	return 0;
}

int
crt_native_rpc_call(struct crt_native_rpc *rpc, uint32_t opcode, const void *data, size_t size,
		    crt_native_done_t done, void *arg, uint64_t *id)
{
	struct entry *e;
	unsigned      slot;
	int           rc;

	if (rpc == NULL || done == NULL || id == NULL || (size != 0 && data == NULL) ||
	    size > rpc->frame_size - CRT_NATIVE_HEADER_SIZE)
		return EINVAL;
	if (rpc->failed)
		return rpc->failed;
	slot = rpc->free_head;
	if (slot == rpc->depth)
		return EAGAIN;
	e = &rpc->requests[slot];
	if (e->generation == UINT32_MAX)
		return EOVERFLOW;
	e->generation++;
	e->id            = (uint64_t)e->generation << 32 | (slot + 1);
	e->opcode        = opcode;
	e->status        = 0;
	e->received      = false;
	e->error         = 0;
	e->response_size = 0;
	e->done          = done;
	e->arg           = arg;
	rc               = encode_frame(rpc, e, false, data, size);
	if (rc != 0)
		return rc;
	rpc->free_head = e->next;
	e->state       = QUEUED;
	rpc->active_calls++;
	enqueue(rpc->requests, &rpc->request_head, &rpc->request_tail, slot, rpc->depth);
	*id = e->id;
	return 0;
}

int
crt_native_rpc_reply(struct crt_native_rpc *rpc, struct crt_native_token token, uint32_t status,
		     const void *data, size_t size)
{
	unsigned      slot;
	struct entry *e;
	int           rc;

	if (rpc == NULL || !token.reply || slot_of(rpc, token.id, &slot) != 0 ||
	    (size != 0 && data == NULL) || size > rpc->frame_size - CRT_NATIVE_HEADER_SIZE)
		return EINVAL;
	if (rpc->failed)
		return rpc->failed;
	e = &rpc->replies[slot];
	if (e->state != HANDLING || e->id != token.id)
		return ENOENT;
	e->status = status;
	rc        = encode_frame(rpc, e, true, data, size);
	if (rc != 0)
		return rc;
	e->state = QUEUED;
	enqueue(rpc->replies, &rpc->reply_head, &rpc->reply_tail, slot, rpc->depth);
	return 0;
}

int
crt_native_rpc_next_tx(struct crt_native_rpc *rpc, struct crt_native_tx *tx)
{
	unsigned      slot;
	bool          reply = true;
	struct entry *e;

	if (rpc == NULL || tx == NULL)
		return EINVAL;
	if (rpc->failed)
		return rpc->failed;
	/* Replies have independent records and priority over new requests. */
	slot = dequeue(rpc->replies, &rpc->reply_head, &rpc->reply_tail, rpc->depth);
	if (slot == rpc->depth) {
		reply = false;
		slot  = dequeue(rpc->requests, &rpc->request_head, &rpc->request_tail, rpc->depth);
	}
	if (slot == rpc->depth)
		return EAGAIN;
	e        = reply ? &rpc->replies[slot] : &rpc->requests[slot];
	e->state = IN_FLIGHT;
	rpc->driver_owned++;
	*tx = (struct crt_native_tx){.token = {e->id, reply}, .data = e->frame, .length = e->size};
	return 0;
}

int
crt_native_rpc_tx_complete(struct crt_native_rpc *rpc, struct crt_native_token token, int error)
{
	struct entry *e;
	unsigned      slot;

	if (rpc == NULL || rpc->callback || error < 0 || slot_of(rpc, token.id, &slot) != 0)
		return EINVAL;
	e = token.reply ? &rpc->replies[slot] : &rpc->requests[slot];
	if (e->state != IN_FLIGHT || e->id != token.id)
		return ENOENT;
	rpc->driver_owned--;
	if (token.reply) {
		e->state = FREE;
		rpc->active_servers--;
		return error;
	}
	if (e->error == 0)
		e->error = error;
	e->state = WAIT_REPLY;
	if (e->error != 0 || e->received)
		finish(rpc, slot);
	return 0;
}

int
crt_native_rpc_receive(struct crt_native_rpc *rpc, const void *frame, size_t size)
{
	struct crt_native_header h;
	struct entry            *e;
	unsigned                 slot;
	int                      rc;

	if (rpc == NULL || rpc->callback || size > rpc->frame_size)
		return EINVAL;
	if (rpc->failed)
		return rpc->failed;
	rc = crt_native_decode(frame, size, &h);
	if (rc != 0 || slot_of(rpc, h.request_id, &slot) != 0)
		return EPROTO;
	if (h.type == CRT_NATIVE_REPLY) {
		e = &rpc->requests[slot];
		/* Late replies to canceled/completed generations do not match new calls. */
		if (e->id != h.request_id || e->state == FREE)
			return 0;
		if (e->state == QUEUED || e->received || e->opcode != h.opcode)
			return EPROTO;
		e->received      = true;
		e->status        = h.status;
		e->response_size = h.payload_size;
		memcpy(e->response, (const uint8_t *)frame + CRT_NATIVE_HEADER_SIZE,
		       h.payload_size);
		if (e->state == WAIT_REPLY)
			finish(rpc, slot);
		return 0;
	}
	e = &rpc->replies[slot];
	if (e->state != FREE)
		return e->id == h.request_id ? EPROTO : EAGAIN;
	if (h.request_id <= e->id)
		return EPROTO;
	e->id     = h.request_id;
	e->opcode = h.opcode;
	e->state  = HANDLING;
	rpc->active_servers++;
	rpc->callback = true;
	rpc->handler(rpc->handler_arg, rpc, (struct crt_native_token){h.request_id, true}, h.opcode,
		     (const uint8_t *)frame + CRT_NATIVE_HEADER_SIZE, h.payload_size);
	rpc->callback = false;
	return 0;
}

int
crt_native_rpc_cancel(struct crt_native_rpc *rpc, uint64_t id, int error)
{
	unsigned      slot;
	struct entry *e;

	if (rpc == NULL || error <= 0 || slot_of(rpc, id, &slot) != 0)
		return EINVAL;
	e = &rpc->requests[slot];
	if (e->state == FREE || e->id != id)
		return ENOENT;
	if (rpc->callback)
		return EBUSY;
	if (e->error == 0)
		e->error = error;
	if (e->state == IN_FLIGHT)
		return 0;
	if (e->state == QUEUED) {
		unsigned *link     = &rpc->request_head;
		unsigned  previous = rpc->depth;
		while (*link != slot) {
			if (*link == rpc->depth)
				return EPROTO;
			previous = *link;
			link     = &rpc->requests[*link].next;
		}
		*link = e->next;
		if (rpc->request_tail == slot)
			rpc->request_tail = previous;
	}
	finish(rpc, slot);
	return 0;
}

int
crt_native_rpc_fail(struct crt_native_rpc *rpc, int error)
{
	if (rpc == NULL || error <= 0 || rpc->callback)
		return EINVAL;
	if (rpc->failed == 0)
		rpc->failed = error;
	for (unsigned i = 0; i < rpc->depth; i++) {
		struct entry *e = &rpc->requests[i];
		if (e->state != FREE)
			crt_native_rpc_cancel(rpc, e->id, rpc->failed);
		e = &rpc->replies[i];
		if (e->state != FREE && e->state != IN_FLIGHT) {
			e->state = FREE;
			rpc->active_servers--;
		}
	}
	rpc->reply_head = rpc->reply_tail = rpc->depth;
	return 0;
}

int
crt_native_rpc_destroy(struct crt_native_rpc *rpc)
{
	if (rpc == NULL)
		return EINVAL;
	if (rpc->active_calls || rpc->active_servers || rpc->driver_owned || rpc->callback)
		return EBUSY;
	free(rpc->buffers);
	free(rpc->replies);
	free(rpc->requests);
	free(rpc);
	return 0;
}

bool
crt_native_rpc_idle(const struct crt_native_rpc *rpc)
{
	return rpc != NULL && rpc->active_calls == 0 && rpc->active_servers == 0 &&
	       rpc->driver_owned == 0 && !rpc->callback;
}

int
crt_native_rpc_buffers(struct crt_native_rpc *rpc, void **base, size_t *size)
{
	if (rpc == NULL || base == NULL || size == NULL)
		return EINVAL;
	*base = rpc->buffers;
	*size = 3 * rpc->depth * rpc->frame_size;
	return 0;
}
