/*
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#include "rpc.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

enum state { FREE, HANDLING, QUEUED, IN_FLIGHT, WAIT_REPLY, FINISHING };
struct entry {
	enum state        state;
	unsigned          next;
	uint64_t          id;
	uint64_t          deadline;
	unsigned          heap_index;
	uint32_t          generation, opcode, status;
	size_t            size, response_size;
	int               error;
	bool              received;
	crt_native_done_t done;
	void             *arg;
	uint8_t          *frame, *response;
	const void       *response_view;
	void             *rx_cookie;
	uint64_t          release_ids[32];
	unsigned          release_count;
};
struct crt_native_rpc {
	unsigned             depth, free_head, request_head, request_tail, reply_head, reply_tail;
	unsigned             active_calls, active_servers, driver_owned;
	size_t               frame_size;
	int                  failed;
	struct entry        *requests, *replies;
	struct entry        *controls;
	unsigned             control_free, control_head, control_tail, active_controls;
	unsigned             pending_releases;
	crt_native_release_t release;
	void                *release_arg;
	uint8_t             *buffers;
	crt_native_handler_t handler;
	void                *handler_arg;
	bool                 callback, encoding;
	unsigned            *deadlines;
	unsigned             deadline_count;
	uint64_t             now_ns;
	struct crt_native_receive_hooks rx_hooks;
	struct crt_native_rpc_stats     stats;
};

static void
put_release(uint8_t *p, uint64_t id)
{
	for (unsigned i = 0; i < 8; i++)
		p[i] = id >> (56 - 8 * i);
}
static uint64_t
get_release(const uint8_t *p)
{
	uint64_t id = 0;
	for (unsigned i = 0; i < 8; i++)
		id = id << 8 | p[i];
	return id;
}

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

static bool
earlier(struct crt_native_rpc *rpc, unsigned a, unsigned b)
{
	struct entry *x = &rpc->requests[rpc->deadlines[a]], *y = &rpc->requests[rpc->deadlines[b]];
	return x->deadline < y->deadline || (x->deadline == y->deadline && x->id < y->id);
}

static void
swap_deadlines(struct crt_native_rpc *rpc, unsigned a, unsigned b)
{
	unsigned slot                               = rpc->deadlines[a];
	rpc->deadlines[a]                           = rpc->deadlines[b];
	rpc->deadlines[b]                           = slot;
	rpc->requests[rpc->deadlines[a]].heap_index = a;
	rpc->requests[rpc->deadlines[b]].heap_index = b;
}

static void
repair_deadline(struct crt_native_rpc *rpc, unsigned at)
{
	while (at != 0 && earlier(rpc, at, (at - 1) / 2)) {
		unsigned parent = (at - 1) / 2;
		swap_deadlines(rpc, at, parent);
		at = parent;
	}
	for (;;) {
		unsigned child = at * 2 + 1;
		if (child >= rpc->deadline_count)
			break;
		if (child + 1 < rpc->deadline_count && earlier(rpc, child + 1, child))
			child++;
		if (!earlier(rpc, child, at))
			break;
		swap_deadlines(rpc, at, child);
		at = child;
	}
}

static void
remove_deadline(struct crt_native_rpc *rpc, struct entry *e)
{
	unsigned index = e->heap_index;
	e->deadline    = 0;
	e->heap_index  = rpc->depth;
	if (index == rpc->depth)
		return;
	unsigned last = rpc->deadlines[--rpc->deadline_count];
	if (index == rpc->deadline_count)
		return;
	rpc->deadlines[index]          = last;
	rpc->requests[last].heap_index = index;
	repair_deadline(rpc, index);
}

static void
finish_payload(struct crt_native_rpc *rpc, unsigned slot, const void *response)
{
	struct entry     *e      = &rpc->requests[slot];
	crt_native_done_t done   = e->done;
	void             *arg    = e->arg;
	int               error  = e->error;
	uint32_t          status = e->status;
	size_t            size   = error ? 0 : e->response_size;
	remove_deadline(rpc, e);
	e->state = FINISHING;

	/* Keep this slot unavailable until its callback returns, so a reentrant
	 * call cannot overwrite the borrowed response while it is being consumed.
	 */
	rpc->callback = true;
	if (e->rx_cookie)
		rpc->rx_hooks.enter(rpc->rx_hooks.arg, e->rx_cookie);
	done(arg, error, status, response, size);
	if (e->rx_cookie) {
		rpc->rx_hooks.release(rpc->rx_hooks.arg, e->rx_cookie);
		e->rx_cookie     = NULL;
		e->response_view = NULL;
	}
	rpc->callback  = false;
	e->state       = FREE;
	e->next        = rpc->free_head;
	rpc->free_head = slot;
	rpc->active_calls--;
}

static void
finish(struct crt_native_rpc *rpc, unsigned slot)
{
	struct entry *e = &rpc->requests[slot];
	finish_payload(rpc, slot, e->response_view ? e->response_view : e->response);
}

int
crt_native_rpc_receive_hooks(struct crt_native_rpc                 *rpc,
			     const struct crt_native_receive_hooks *hooks)
{
	if (!rpc || !hooks || !hooks->retain || !hooks->enter || !hooks->release ||
	    !crt_native_rpc_idle(rpc))
		return EINVAL;
	rpc->rx_hooks = *hooks;
	return 0;
}

void
crt_native_rpc_get_stats(const struct crt_native_rpc *rpc, struct crt_native_rpc_stats *out)
{
	if (rpc && out)
		*out = rpc->stats;
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
	    frame_size > 1048576 || handler == NULL || frame_size > SIZE_MAX / (4 * depth))
		return EINVAL;
	rpc = calloc(1, sizeof(*rpc));
	if (rpc == NULL)
		return ENOMEM;
	rpc->requests  = calloc(depth, sizeof(*rpc->requests));
	rpc->replies   = calloc(depth, sizeof(*rpc->replies));
	rpc->controls  = calloc(depth, sizeof(*rpc->controls));
	rpc->buffers   = calloc(4 * depth, frame_size);
	rpc->deadlines = calloc(depth, sizeof(*rpc->deadlines));
	if (rpc->requests == NULL || rpc->replies == NULL || rpc->controls == NULL ||
	    rpc->buffers == NULL || rpc->deadlines == NULL) {
		free(rpc->deadlines);
		free(rpc->controls);
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
	rpc->control_head = rpc->control_tail = depth;
	for (unsigned i = 0; i < depth; i++) {
		rpc->requests[i].next       = i + 1;
		rpc->requests[i].heap_index = depth;
		rpc->requests[i].frame      = rpc->buffers + i * frame_size;
		rpc->requests[i].response   = rpc->buffers + (depth + i) * frame_size;
		rpc->replies[i].frame       = rpc->buffers + (2 * depth + i) * frame_size;
		rpc->controls[i].frame      = rpc->buffers + (3 * depth + i) * frame_size;
		rpc->controls[i].next       = i + 1;
	}
	*output = rpc;
	return 0;
}

int
crt_native_rpc_set_release_handler(struct crt_native_rpc *rpc, crt_native_release_t release,
				   void *arg)
{
	if (rpc == NULL || release == NULL || rpc->callback || rpc->encoding)
		return EINVAL;
	if (!crt_native_rpc_idle(rpc) || rpc->release != NULL)
		return EBUSY;
	rpc->release     = release;
	rpc->release_arg = arg;
	return 0;
}

int
crt_native_rpc_queue_release(struct crt_native_rpc *rpc, uint64_t id)
{
	if (rpc == NULL || id == 0)
		return EINVAL;
	if (rpc->encoding)
		return EBUSY;
	if (rpc->failed)
		return rpc->failed;
	if (rpc->pending_releases == rpc->depth)
		return EAGAIN;
	if (rpc->control_tail != rpc->depth) {
		struct entry *tail = &rpc->controls[rpc->control_tail];
		if (tail->release_count < 32 &&
		    CRT_NATIVE_HEADER_SIZE + tail->release_count * 8 <= rpc->frame_size) {
			tail->release_ids[tail->release_count++] = id;
			rpc->pending_releases++;
			return 0;
		}
	}
	unsigned slot = rpc->control_free;
	if (slot == rpc->depth)
		return EAGAIN;
	struct entry *e = &rpc->controls[slot];
	if (e->generation == UINT32_MAX)
		return EOVERFLOW;
	e->generation++;
	e->id             = (uint64_t)e->generation << 32 | (slot + 1);
	e->state          = QUEUED;
	e->release_ids[0] = id;
	e->release_count  = 1;
	rpc->control_free = e->next;
	rpc->active_controls++;
	rpc->pending_releases++;
	enqueue(rpc->controls, &rpc->control_head, &rpc->control_tail, slot, rpc->depth);
	return 0;
}

static int
encode_frame(struct crt_native_rpc *rpc, struct entry *e, bool reply, crt_native_encoder_t encode,
	     void *arg)
{
	size_t size = 0;
	int    rc;
	if (rpc->encoding)
		return EBUSY;
	rpc->encoding = true;
	rc            = encode(arg, e->frame + CRT_NATIVE_HEADER_SIZE,
			       rpc->frame_size - CRT_NATIVE_HEADER_SIZE, &size);
	rpc->encoding = false;
	if (rc != 0)
		return rc > 0 ? rc : EINVAL;
	if (size > rpc->frame_size - CRT_NATIVE_HEADER_SIZE)
		return EMSGSIZE;
	struct crt_native_header h = {
	    .type         = reply ? CRT_NATIVE_REPLY : CRT_NATIVE_REQUEST,
	    .request_id   = e->id,
	    .opcode       = e->opcode,
	    .payload_size = (uint32_t)size,
	    .status       = e->status,
	};
	rc = crt_native_encode(&h, e->frame, rpc->frame_size);
	if (rc != 0)
		return rc;
	e->release_count = 0;
	e->size          = CRT_NATIVE_HEADER_SIZE + size;
	return 0;
}

int
crt_native_rpc_call_encoded(struct crt_native_rpc *rpc, uint32_t opcode,
			    crt_native_encoder_t encode, void *encoder_arg, crt_native_done_t done,
			    void *arg, uint64_t *id)
{
	struct entry *e;
	unsigned      slot;
	int           rc;

	if (rpc == NULL || done == NULL || id == NULL || encode == NULL)
		return EINVAL;
	if (rpc->encoding)
		return EBUSY;
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
	e->response_view = NULL;
	e->rx_cookie     = NULL;
	e->done          = done;
	e->arg           = arg;
	rc               = encode_frame(rpc, e, false, encode, encoder_arg);
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
crt_native_rpc_reply_encoded(struct crt_native_rpc *rpc, struct crt_native_token token,
			     uint32_t status, crt_native_encoder_t encode, void *arg)
{
	unsigned      slot;
	struct entry *e;
	int           rc;

	if (rpc == NULL || !token.reply || token.control || slot_of(rpc, token.id, &slot) != 0 ||
	    encode == NULL)
		return EINVAL;
	if (rpc->encoding)
		return EBUSY;
	if (rpc->failed)
		return rpc->failed;
	e = &rpc->replies[slot];
	if (e->state != HANDLING || e->id != token.id)
		return ENOENT;
	e->status = status;
	rc        = encode_frame(rpc, e, true, encode, arg);
	if (rc != 0)
		return rc;
	e->state = QUEUED;
	enqueue(rpc->replies, &rpc->reply_head, &rpc->reply_tail, slot, rpc->depth);
	return 0;
}

struct copy_payload {
	const void *data;
	size_t      size;
};

static int
copy_payload(void *arg, void *buffer, size_t capacity, size_t *used)
{
	struct copy_payload *p = arg;
	if (p->size > capacity || (p->size != 0 && p->data == NULL))
		return EINVAL;
	if (p->size)
		memcpy(buffer, p->data, p->size);
	*used = p->size;
	return 0;
}

int
crt_native_rpc_call(struct crt_native_rpc *rpc, uint32_t opcode, const void *data, size_t size,
		    crt_native_done_t done, void *arg, uint64_t *id)
{
	struct copy_payload p = {data, size};
	/* Preserve validation before consuming a generation. */
	if (rpc == NULL || size > rpc->frame_size - CRT_NATIVE_HEADER_SIZE ||
	    (size && data == NULL))
		return EINVAL;
	return crt_native_rpc_call_encoded(rpc, opcode, copy_payload, &p, done, arg, id);
}

int
crt_native_rpc_reply(struct crt_native_rpc *rpc, struct crt_native_token token, uint32_t status,
		     const void *data, size_t size)
{
	struct copy_payload p = {data, size};
	if (rpc == NULL || size > rpc->frame_size - CRT_NATIVE_HEADER_SIZE ||
	    (size && data == NULL))
		return EINVAL;
	return crt_native_rpc_reply_encoded(rpc, token, status, copy_payload, &p);
}

static void
attach_releases(struct crt_native_rpc *rpc, struct entry *e, bool reply)
{
	size_t payload = e->size - CRT_NATIVE_HEADER_SIZE;
	while (rpc->control_head != rpc->depth && e->release_count < 32 &&
	       e->size + 8 <= rpc->frame_size) {
		unsigned      slot    = rpc->control_head;
		struct entry *control = &rpc->controls[slot];
		uint64_t      id      = control->release_ids[0];
		memmove(control->release_ids, control->release_ids + 1,
			(control->release_count - 1) * 8);
		control->release_count--;
		put_release(e->frame + e->size, id);
		e->release_ids[e->release_count++] = id;
		e->size += 8;
		if (!control->release_count) {
			dequeue(rpc->controls, &rpc->control_head, &rpc->control_tail, rpc->depth);
			control->state    = FREE;
			control->next     = rpc->control_free;
			rpc->control_free = slot;
			rpc->active_controls--;
		}
	}
	struct crt_native_header h = {.type         = reply ? CRT_NATIVE_REPLY : CRT_NATIVE_REQUEST,
				      .request_id   = e->id,
				      .opcode       = e->opcode,
				      .payload_size = payload,
				      .status       = e->status,
				      .release_count = e->release_count};
	crt_native_encode(&h, e->frame, rpc->frame_size);
	rpc->stats.piggyback_ids += e->release_count;
}

static int
complete_releases(struct crt_native_rpc *rpc, struct entry *e, int error)
{
	unsigned count = e->release_count;
	uint64_t ids[32];
	if (count > rpc->pending_releases)
		return EPROTO;
	memcpy(ids, e->release_ids, count * 8);
	e->release_count = 0;
	rpc->pending_releases -= count;
	/* A carrier canceled while waiting for SQ space never reached the peer.
	 * Restore its ACKs; reservations prevent unrelated ACKs taking that space.
	 * Repeated acknowledgments after an ambiguous error are idempotent.
	 */
	if (error && !rpc->failed)
		for (unsigned i = 0; i < count; i++) {
			int rc = crt_native_rpc_queue_release(rpc, ids[i]);
			if (rc)
				return rc;
		}
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
	if (rpc->encoding)
		return EBUSY;
	if (rpc->failed)
		return rpc->failed;
	/* Releases make progress independently, including when every RPC slot is busy. */
	struct entry *candidate = rpc->reply_head != rpc->depth ? &rpc->replies[rpc->reply_head]
				  : rpc->request_head != rpc->depth
				      ? &rpc->requests[rpc->request_head]
				      : NULL;
	bool          carrier   = candidate && candidate->size + 8 <= rpc->frame_size &&
		       (!candidate->deadline || candidate->deadline > rpc->now_ns);
	slot = carrier ? rpc->depth
		       : dequeue(rpc->controls, &rpc->control_head, &rpc->control_tail, rpc->depth);
	if (slot != rpc->depth) {
		e                          = &rpc->controls[slot];
		struct crt_native_header h = {.type          = CRT_NATIVE_RELEASE,
					      .request_id    = e->release_ids[0],
					      .release_count = e->release_count - 1};
		crt_native_encode(&h, e->frame, rpc->frame_size);
		for (unsigned i = 1; i < e->release_count; i++)
			put_release(e->frame + CRT_NATIVE_HEADER_SIZE + (i - 1) * 8,
				    e->release_ids[i]);
		e->size = CRT_NATIVE_HEADER_SIZE + (e->release_count - 1) * 8;
		rpc->stats.release_frames++;
		rpc->stats.release_ids += e->release_count;
		e->state = IN_FLIGHT;
		rpc->driver_owned++;
		*tx = (struct crt_native_tx){
		    .token = {.id = e->id, .control = true}, .data = e->frame, .length = e->size};
		return 0;
	}
	/* Replies have independent records and priority over new requests. */
	slot = dequeue(rpc->replies, &rpc->reply_head, &rpc->reply_tail, rpc->depth);
	if (slot == rpc->depth) {
		reply = false;
		/* expire() is bounded. Do not admit another already-due request
		 * while remaining expirations await the next progress invocation.
		 */
		if (rpc->request_head != rpc->depth) {
			struct entry *head = &rpc->requests[rpc->request_head];
			if (head->deadline && head->deadline <= rpc->now_ns)
				return EAGAIN;
		}
		slot = dequeue(rpc->requests, &rpc->request_head, &rpc->request_tail, rpc->depth);
	}
	if (slot == rpc->depth)
		return EAGAIN;
	e = reply ? &rpc->replies[slot] : &rpc->requests[slot];
	if (rpc->control_head != rpc->depth)
		attach_releases(rpc, e, reply);
	if (reply)
		rpc->stats.reply_frames++;
	else
		rpc->stats.request_frames++;
	e->state = IN_FLIGHT;
	rpc->driver_owned++;
	*tx = (struct crt_native_tx){
	    .token = {.id = e->id, .reply = reply}, .data = e->frame, .length = e->size};
	return 0;
}

int
crt_native_rpc_tx_status(struct crt_native_rpc *rpc, struct crt_native_token token)
{
	unsigned slot;
	if (rpc == NULL || slot_of(rpc, token.id, &slot) != 0)
		return EINVAL;
	struct entry *e = token.control ? &rpc->controls[slot]
			  : token.reply ? &rpc->replies[slot]
					: &rpc->requests[slot];
	if (e->state != IN_FLIGHT || e->id != token.id)
		return ENOENT;
	if (token.control || token.reply)
		return 0;
	if (e->error)
		return e->error;
	return e->deadline && e->deadline <= rpc->now_ns ? ETIMEDOUT : 0;
}

int
crt_native_rpc_tx_complete(struct crt_native_rpc *rpc, struct crt_native_token token, int error)
{
	struct entry *e;
	unsigned      slot;

	if (rpc == NULL || rpc->callback || error < 0 || slot_of(rpc, token.id, &slot) != 0)
		return EINVAL;
	if (rpc->encoding)
		return EBUSY;
	if (token.control) {
		e = &rpc->controls[slot];
		if (token.reply || e->state != IN_FLIGHT || e->id != token.id)
			return ENOENT;
		e->state          = FREE;
		e->next           = rpc->control_free;
		rpc->control_free = slot;
		rpc->active_controls--;
		rpc->driver_owned--;
		int rc = complete_releases(rpc, e, error);
		return rc ? rc : error;
	}
	e = token.reply ? &rpc->replies[slot] : &rpc->requests[slot];
	if (e->state != IN_FLIGHT || e->id != token.id)
		return ENOENT;
	int releases_rc = complete_releases(rpc, e, error);
	if (releases_rc && !error)
		error = releases_rc;
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
	return releases_rc;
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
	if (rpc->encoding)
		return EBUSY;
	if (rpc->failed)
		return rpc->failed;
	rc = crt_native_decode(frame, size, &h);
	if (rc != 0)
		return EPROTO;
	if (h.type == CRT_NATIVE_RELEASE || h.release_count) {
		if (!rpc->release)
			return EPROTO;
		const uint8_t *ids =
		    (const uint8_t *)frame + CRT_NATIVE_HEADER_SIZE + h.payload_size;
		rpc->callback = true;
		rc =
		    h.type == CRT_NATIVE_RELEASE ? rpc->release(rpc->release_arg, h.request_id) : 0;
		for (unsigned i = 0; i < h.release_count && !rc; i++)
			rc = rpc->release(rpc->release_arg, get_release(ids + 8 * i));
		rpc->callback = false;
		if (rc || h.type == CRT_NATIVE_RELEASE)
			return rc;
	}
	if (slot_of(rpc, h.request_id, &slot) != 0)
		return EPROTO;
	if (h.type == CRT_NATIVE_REPLY) {
		e = &rpc->requests[slot];
		/* Late replies to canceled/completed generations do not match new calls. */
		if (e->id != h.request_id || e->state == FREE)
			return 0;
		if (e->state == QUEUED || e->received || e->opcode != h.opcode)
			return EPROTO;
		e->received         = true;
		e->status           = h.status;
		e->response_size    = h.payload_size;
		const void *payload = (const uint8_t *)frame + CRT_NATIVE_HEADER_SIZE;
		if (e->state == WAIT_REPLY)
			/* SEND is complete: callback consumes RX before repost. */
			finish_payload(rpc, slot, payload);
		else if (rpc->rx_hooks.retain &&
			 rpc->rx_hooks.retain(rpc->rx_hooks.arg, payload, h.payload_size,
					      &e->rx_cookie) == 0) {
			e->response_view = payload;
			rpc->stats.early_retained++;
		} else {
			/* Standalone users without RX ownership hooks retain the fallback. */
			memcpy(e->response, payload, h.payload_size);
			rpc->stats.early_copies++;
		}
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
	rpc->handler(rpc->handler_arg, rpc,
		     (struct crt_native_token){.id = h.request_id, .reply = true}, h.opcode,
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
	if (rpc->callback || rpc->encoding)
		return EBUSY;
	remove_deadline(rpc, e);
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
crt_native_rpc_set_deadline(struct crt_native_rpc *rpc, uint64_t id, uint64_t deadline)
{
	unsigned slot;
	if (rpc == NULL || slot_of(rpc, id, &slot) != 0)
		return EINVAL;
	if (rpc->encoding)
		return EBUSY;
	struct entry *e = &rpc->requests[slot];
	if (e->id != id || (e->state != QUEUED && e->state != IN_FLIGHT && e->state != WAIT_REPLY))
		return ENOENT;
	if (e->error)
		return EALREADY;
	remove_deadline(rpc, e);
	if (deadline == 0)
		return 0;
	e->deadline                           = deadline;
	e->heap_index                         = rpc->deadline_count;
	rpc->deadlines[rpc->deadline_count++] = slot;
	repair_deadline(rpc, e->heap_index);
	return 0;
}

uint64_t
crt_native_rpc_next_deadline(const struct crt_native_rpc *rpc)
{
	return rpc == NULL || rpc->deadline_count == 0 ? 0
						       : rpc->requests[rpc->deadlines[0]].deadline;
}

int
crt_native_rpc_expire(struct crt_native_rpc *rpc, uint64_t now, unsigned *expired)
{
	if (rpc == NULL || expired == NULL)
		return EINVAL;
	*expired = 0;
	if (rpc->callback || rpc->encoding)
		return EBUSY;
	if (now > rpc->now_ns)
		rpc->now_ns = now;
	while (*expired < 32 && rpc->deadline_count && crt_native_rpc_next_deadline(rpc) <= now) {
		uint64_t id = rpc->requests[rpc->deadlines[0]].id;
		int      rc = crt_native_rpc_cancel(rpc, id, ETIMEDOUT);
		if (rc != 0)
			return rc;
		(*expired)++;
	}
	return 0;
}

int
crt_native_rpc_fail(struct crt_native_rpc *rpc, int error)
{
	if (rpc == NULL || error <= 0 || rpc->callback)
		return EINVAL;
	if (rpc->encoding)
		return EBUSY;
	if (rpc->failed == 0)
		rpc->failed = error;
	for (unsigned i = 0; i < rpc->depth; i++) {
		struct entry *control = &rpc->controls[i];
		if (control->state == QUEUED) {
			rpc->pending_releases -= control->release_count;
			control->release_count = 0;
			control->state         = FREE;
			control->next          = rpc->control_free;
			rpc->control_free      = i;
			rpc->active_controls--;
		}
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
	rpc->control_head = rpc->control_tail = rpc->depth;
	return 0;
}

int
crt_native_rpc_destroy(struct crt_native_rpc *rpc)
{
	if (rpc == NULL)
		return EINVAL;
	if (rpc->active_calls || rpc->active_servers || rpc->active_controls ||
	    rpc->pending_releases || rpc->driver_owned || rpc->callback || rpc->encoding)
		return EBUSY;
	free(rpc->buffers);
	free(rpc->deadlines);
	free(rpc->controls);
	free(rpc->replies);
	free(rpc->requests);
	free(rpc);
	return 0;
}

bool
crt_native_rpc_idle(const struct crt_native_rpc *rpc)
{
	return rpc != NULL && rpc->active_calls == 0 && rpc->active_servers == 0 &&
	       rpc->driver_owned == 0 && rpc->active_controls == 0 && rpc->pending_releases == 0 &&
	       !rpc->callback && !rpc->encoding;
}

int
crt_native_rpc_buffers(struct crt_native_rpc *rpc, void **base, size_t *size)
{
	if (rpc == NULL || base == NULL || size == NULL)
		return EINVAL;
	*base = rpc->buffers;
	*size = 4 * rpc->depth * rpc->frame_size;
	return 0;
}
