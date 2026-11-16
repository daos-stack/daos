/*
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#include "bulk_session.h"
#include <errno.h>
#include <limits.h>
#include <stdlib.h>

enum export_state { EXPORT_FREE, DRAFT, PUBLISHED };
struct exported {
	struct crt_native_bulk_lease lease;
	struct crt_native_bulk_owner owner;
	uint32_t                     generation;
	unsigned                     next;
	enum export_state            state;
};
enum import_state { IMPORT_FREE, IMPORTED, ACK_PENDING };
struct imported {
	struct crt_native_bulk_remote remote;
	unsigned                      refs, next;
	enum import_state             state;
};
struct crt_native_bulk_session {
	const struct crt_native_ops *ops;
	struct crt_native_ep        *ep;
	struct exported             *exports;
	struct imported             *imports;
	unsigned                     capacity, free_head, ack_head, ack_tail, exporting, importing;
	crt_native_ack_send_t        send;
	void                        *send_arg;
	bool                         closing, quiesced, callback;
};

bool
crt_native_bulk_session_on_endpoint(const struct crt_native_bulk_session *s,
				    const struct crt_native_ops          *ops,
				    const struct crt_native_ep           *ep)
{
	return s != NULL && s->ops == ops && s->ep == ep;
}

static int
slot_of(const struct crt_native_bulk_session *s, uint64_t id, unsigned *slot)
{
	uint32_t low = (uint32_t)id;
	if (s == NULL || id >> 32 == 0 || low == 0 || low > s->capacity)
		return EINVAL;
	*slot = low - 1;
	return 0;
}

int
crt_native_bulk_session_create(const struct crt_native_ops *ops, struct crt_native_ep *ep,
			       unsigned capacity, crt_native_ack_send_t send, void *arg,
			       struct crt_native_bulk_session **out)
{
	if (out == NULL)
		return EINVAL;
	*out = NULL;
	if (ops == NULL || ops->close_begin == NULL || ops->quiesce == NULL || ep == NULL ||
	    capacity == 0 || capacity > 32768 || send == NULL)
		return EINVAL;
	struct crt_native_bulk_session *s = calloc(1, sizeof(*s));
	if (s == NULL)
		return ENOMEM;
	s->exports = calloc(capacity, sizeof(*s->exports));
	s->imports = calloc(capacity, sizeof(*s->imports));
	if (s->exports == NULL || s->imports == NULL) {
		free(s->exports);
		free(s->imports);
		free(s);
		return ENOMEM;
	}
	s->ops      = ops;
	s->ep       = ep;
	s->capacity = capacity;
	s->send     = send;
	s->send_arg = arg;
	s->ack_head = s->ack_tail = capacity;
	for (unsigned i = 0; i < capacity; i++)
		s->exports[i].next = i + 1;
	*out = s;
	return 0;
}

int
crt_native_bulk_export_begin(struct crt_native_bulk_session *s, struct crt_native_bulk *bulk,
			     const struct crt_native_bulk_owner *owner, uint64_t *id)
{
	if (s == NULL || bulk == NULL || id == NULL ||
	    (owner != NULL && (owner->hold == NULL || owner->drop == NULL)))
		return EINVAL;
	if (s->callback)
		return EBUSY;
	if (s->closing)
		return ESHUTDOWN;
	if (!crt_native_bulk_on_endpoint(bulk, s->ops, s->ep))
		return EXDEV;
	unsigned slot = s->free_head;
	if (slot == s->capacity)
		return EAGAIN;
	struct exported *e = &s->exports[slot];
	if (e->generation == UINT32_MAX)
		return EOVERFLOW;
	uint64_t next_id = (uint64_t)(e->generation + 1) << 32 | (slot + 1);
	int      rc      = crt_native_bulk_acquire(bulk, next_id, &e->lease);
	if (rc != 0)
		return rc;
	e->generation++;
	e->state     = DRAFT;
	e->owner     = owner == NULL ? (struct crt_native_bulk_owner){0} : *owner;
	s->free_head = e->next;
	s->exporting++;
	if (e->owner.hold != NULL) {
		s->callback = true;
		e->owner.hold(e->owner.arg);
		s->callback = false;
	}
	*id = next_id;
	return 0;
}

static struct exported *
find_export(struct crt_native_bulk_session *s, uint64_t id, unsigned *slot)
{
	if (slot_of(s, id, slot) != 0)
		return NULL;
	struct exported *e = &s->exports[*slot];
	return e->state != EXPORT_FREE && e->lease.id == id ? e : NULL;
}

int
crt_native_bulk_export_pack(struct crt_native_bulk_session *s, uint64_t id, void *buffer,
			    size_t capacity, size_t *used)
{
	unsigned         slot;
	struct exported *e = find_export(s, id, &slot);
	if (e == NULL)
		return ENOENT;
	if (s->callback)
		return EBUSY;
	if (s->closing)
		return ESHUTDOWN;
	return crt_native_bulk_pack(&e->lease, buffer, capacity, used);
}

int
crt_native_bulk_export_publish(struct crt_native_bulk_session *s, uint64_t id)
{
	unsigned         slot;
	struct exported *e = find_export(s, id, &slot);
	if (e == NULL)
		return ENOENT;
	if (s->callback)
		return EBUSY;
	if (s->closing)
		return ESHUTDOWN;
	if (e->state != DRAFT)
		return EALREADY;
	e->state = PUBLISHED;
	return 0;
}

static void
release_export(struct crt_native_bulk_session *s, unsigned slot)
{
	struct exported             *e     = &s->exports[slot];
	struct crt_native_bulk_owner owner = e->owner;
	crt_native_bulk_release(&e->lease);
	e->state     = EXPORT_FREE;
	e->next      = s->free_head;
	s->free_head = slot;
	s->exporting--;
	if (owner.drop != NULL) {
		s->callback = true;
		owner.drop(owner.arg);
		s->callback = false;
	}
}

int
crt_native_bulk_export_abort(struct crt_native_bulk_session *s, uint64_t id)
{
	unsigned         slot;
	struct exported *e = find_export(s, id, &slot);
	if (e == NULL)
		return ENOENT;
	if (s->callback)
		return EBUSY;
	if (e->state != DRAFT)
		return EBUSY;
	release_export(s, slot);
	return 0;
}

int
crt_native_bulk_session_ack(void *arg, uint64_t id)
{
	struct crt_native_bulk_session *s = arg;
	unsigned                        slot;
	if (slot_of(s, id, &slot) != 0)
		return EPROTO;
	if (s->callback)
		return EBUSY;
	struct exported *e          = &s->exports[slot];
	uint32_t         generation = id >> 32;
	if (generation > e->generation)
		return EPROTO;
	if (generation < e->generation || e->state == EXPORT_FREE)
		return 0;
	if (e->state != PUBLISHED)
		return EPROTO;
	release_export(s, slot);
	return 0;
}

static bool
valid_remote(const struct crt_native_bulk_remote *r)
{
	uint64_t total = 0;
	if (r == NULL || r->count == 0 || r->count > CRT_NATIVE_BULK_MAX_SEGMENTS ||
	    r->access == 0 || (r->access & ~(CRT_NATIVE_REMOTE_READ | CRT_NATIVE_REMOTE_WRITE)))
		return false;
	for (unsigned i = 0; i < r->count; i++) {
		const struct crt_native_remote_memory *m = &r->segments[i];
		if (m->length == 0 || m->length > UINT64_MAX - m->address ||
		    m->length > UINT64_MAX - total)
			return false;
		total += m->length;
	}
	return total == r->length;
}

static bool
same_remote(const struct crt_native_bulk_remote *a, const struct crt_native_bulk_remote *b)
{
	if (a->length != b->length || a->count != b->count || a->access != b->access)
		return false;
	for (unsigned i = 0; i < a->count; i++)
		if (a->segments[i].address != b->segments[i].address ||
		    a->segments[i].length != b->segments[i].length ||
		    a->segments[i].key != b->segments[i].key)
			return false;
	return true;
}

int
crt_native_bulk_import(struct crt_native_bulk_session      *s,
		       const struct crt_native_bulk_remote *remote)
{
	unsigned slot;
	if (!valid_remote(remote) || slot_of(s, remote->lease_id, &slot) != 0)
		return EPROTO;
	if (s->callback)
		return EBUSY;
	if (s->closing)
		return ESHUTDOWN;
	struct imported *i = &s->imports[slot];
	if (i->state != IMPORT_FREE) {
		if (i->state != IMPORTED || remote->lease_id != i->remote.lease_id ||
		    !same_remote(remote, &i->remote))
			return EPROTO;
		if (i->refs == UINT_MAX)
			return EOVERFLOW;
		i->refs++;
		return 0;
	}
	if (remote->lease_id <= i->remote.lease_id)
		return EPROTO;
	i->remote = *remote;
	i->state  = IMPORTED;
	i->refs   = 1;
	s->importing++;
	return 0;
}

int
crt_native_bulk_import_get(struct crt_native_bulk_session *s, uint64_t id,
			   const struct crt_native_bulk_remote **remote)
{
	unsigned slot;
	if (remote == NULL || slot_of(s, id, &slot) != 0)
		return EINVAL;
	if (s->callback)
		return EBUSY;
	if (s->closing)
		return ESHUTDOWN;
	struct imported *i = &s->imports[slot];
	if (i->state != IMPORTED || i->remote.lease_id != id)
		return ENOENT;
	if (i->refs == UINT_MAX)
		return EOVERFLOW;
	i->refs++;
	*remote = &i->remote;
	return 0;
}

int
crt_native_bulk_import_put(struct crt_native_bulk_session *s, uint64_t id)
{
	unsigned slot;
	if (slot_of(s, id, &slot) != 0)
		return EINVAL;
	if (s->callback)
		return EBUSY;
	struct imported *i = &s->imports[slot];
	if (i->state != IMPORTED || i->remote.lease_id != id || i->refs == 0)
		return ENOENT;
	if (--i->refs != 0)
		return 0;
	if (s->closing) {
		i->state = IMPORT_FREE;
		s->importing--;
		return 0;
	}
	i->state = ACK_PENDING;
	i->next  = s->capacity;
	if (s->ack_tail != s->capacity)
		s->imports[s->ack_tail].next = slot;
	else
		s->ack_head = slot;
	s->ack_tail = slot;
	return 0;
}

int
crt_native_bulk_session_progress(struct crt_native_bulk_session *s)
{
	if (s == NULL)
		return EINVAL;
	if (s->callback)
		return EBUSY;
	if (s->closing)
		return ESHUTDOWN;
	for (unsigned budget = 0; budget < 32 && s->ack_head != s->capacity; budget++) {
		struct imported *i = &s->imports[s->ack_head];
		s->callback        = true;
		int rc             = s->send(s->send_arg, i->remote.lease_id);
		s->callback        = false;
		if (rc != 0)
			return rc;
		s->ack_head = i->next;
		if (s->ack_head == s->capacity)
			s->ack_tail = s->capacity;
		i->state = IMPORT_FREE;
		s->importing--;
	}
	return 0;
}

int
crt_native_bulk_session_shutdown(struct crt_native_bulk_session *s)
{
	if (s == NULL)
		return EINVAL;
	if (s->callback)
		return EBUSY;
	s->closing = true;
	return s->ops->close_begin(s->ep);
}

int
crt_native_bulk_session_quiesce(struct crt_native_bulk_session *s)
{
	if (s == NULL || !s->closing)
		return EINVAL;
	if (s->callback)
		return EBUSY;
	if (s->quiesced)
		return 0;
	int rc = s->ops->quiesce(s->ep);
	if (rc != 0)
		return rc;
	s->quiesced = true;
	for (unsigned i = 0; i < s->capacity; i++) {
		if (s->exports[i].state != EXPORT_FREE)
			release_export(s, i);
		if (s->imports[i].state == ACK_PENDING) {
			s->imports[i].state = IMPORT_FREE;
			s->importing--;
		}
	}
	s->ack_head = s->ack_tail = s->capacity;
	return 0;
}

int
crt_native_bulk_session_destroy(struct crt_native_bulk_session *s)
{
	if (s == NULL)
		return EINVAL;
	if (s->callback || s->exporting || s->importing)
		return EBUSY;
	free(s->exports);
	free(s->imports);
	free(s);
	return 0;
}
