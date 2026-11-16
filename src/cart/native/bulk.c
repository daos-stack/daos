/*
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#include "bulk.h"
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

struct crt_native_bulk {
	const struct crt_native_ops    *ops;
	struct crt_native_ep           *ep;
	unsigned                        count, access, leases;
	uint64_t                        length;
	bool                            closing;
	struct crt_native_mr           *mrs[CRT_NATIVE_BULK_MAX_SEGMENTS];
	struct crt_native_remote_memory segments[CRT_NATIVE_BULK_MAX_SEGMENTS];
};

bool
crt_native_bulk_on_endpoint(const struct crt_native_bulk *bulk, const struct crt_native_ops *ops,
			    const struct crt_native_ep *ep)
{
	return bulk != NULL && bulk->ops == ops && bulk->ep == ep;
}

static void
put32(uint8_t *p, uint32_t v)
{
	p[0] = v >> 24;
	p[1] = v >> 16;
	p[2] = v >> 8;
	p[3] = v;
}
static uint32_t
get32(const uint8_t *p)
{
	return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
static void
put64(uint8_t *p, uint64_t v)
{
	put32(p, v >> 32);
	put32(p + 4, v);
}
static uint64_t
get64(const uint8_t *p)
{
	return (uint64_t)get32(p) << 32 | get32(p + 4);
}

int
crt_native_bulk_destroy(struct crt_native_bulk *bulk)
{
	if (bulk == NULL)
		return EINVAL;
	if (bulk->leases != 0)
		return EBUSY;
	bulk->closing = true;
	for (unsigned i = 0; i < bulk->count; i++) {
		int rc;
		if (bulk->mrs[i] == NULL)
			continue;
		rc = bulk->ops->deregister_memory(bulk->mrs[i]);
		if (rc != 0)
			return rc;
		bulk->mrs[i] = NULL;
	}
	free(bulk);
	return 0;
}

int
crt_native_bulk_create(const struct crt_native_ops *ops, struct crt_native_ep *ep,
		       const struct crt_native_iov *iov, unsigned count, unsigned access,
		       struct crt_native_bulk **out)
{
	struct crt_native_bulk *bulk;
	uint64_t                total = 0;
	int                     rc;

	if (out == NULL)
		return EINVAL;
	*out = NULL;
	if (ops == NULL || ops->register_memory == NULL || ops->deregister_memory == NULL ||
	    ep == NULL || iov == NULL || count == 0 || count > CRT_NATIVE_BULK_MAX_SEGMENTS ||
	    access == 0 || (access & ~(CRT_NATIVE_REMOTE_READ | CRT_NATIVE_REMOTE_WRITE)) != 0)
		return EINVAL;
	for (unsigned i = 0; i < count; i++) {
		if (iov[i].base == NULL || iov[i].size == 0 || iov[i].size > UINT64_MAX - total ||
		    iov[i].size > UINTPTR_MAX - (uintptr_t)iov[i].base)
			return EINVAL;
		total += iov[i].size;
	}
	bulk = calloc(1, sizeof(*bulk));
	if (bulk == NULL)
		return ENOMEM;
	bulk->ops    = ops;
	bulk->ep     = ep;
	bulk->count  = count;
	bulk->access = access;
	bulk->length = total;
	for (unsigned i = 0; i < count; i++) {
		rc = ops->register_memory(ep, iov[i].base, iov[i].size, access, &bulk->mrs[i],
					  &bulk->segments[i]);
		if (rc != 0) {
			if (crt_native_bulk_destroy(bulk) != 0)
				*out = bulk;
			return rc;
		}
	}
	*out = bulk;
	return 0;
}

int
crt_native_bulk_acquire(struct crt_native_bulk *bulk, uint64_t id,
			struct crt_native_bulk_lease *lease)
{
	if (bulk == NULL || lease == NULL || id == 0)
		return EINVAL;
	if (bulk->closing)
		return ESHUTDOWN;
	if (lease->owner != NULL)
		return EBUSY;
	if (bulk->leases == UINT_MAX)
		return EOVERFLOW;
	bulk->leases++;
	*lease = (struct crt_native_bulk_lease){bulk, id};
	return 0;
}

int
crt_native_bulk_release(struct crt_native_bulk_lease *lease)
{
	if (lease == NULL || lease->owner == NULL)
		return EINVAL;
	lease->owner->leases--;
	*lease = (struct crt_native_bulk_lease){0};
	return 0;
}

int
crt_native_bulk_pack(const struct crt_native_bulk_lease *lease, void *buffer, size_t capacity,
		     size_t *used)
{
	const struct crt_native_bulk *bulk;
	uint8_t                      *p = buffer;
	size_t                        size;

	if (lease == NULL || lease->owner == NULL || buffer == NULL || used == NULL)
		return EINVAL;
	bulk = lease->owner;
	if (bulk->closing)
		return ESHUTDOWN;
	size = CRT_NATIVE_BULK_HEADER + bulk->count * CRT_NATIVE_BULK_SEGMENT;
	if (capacity < size)
		return EMSGSIZE;
	memset(p, 0, size);
	memcpy(p, "CRTK", 4);
	p[4] = 1;
	p[5] = bulk->access;
	p[6] = bulk->count >> 8;
	p[7] = bulk->count;
	put64(p + 8, lease->id);
	for (unsigned i = 0; i < bulk->count; i++) {
		uint8_t *s = p + CRT_NATIVE_BULK_HEADER + i * CRT_NATIVE_BULK_SEGMENT;
		put64(s, bulk->segments[i].address);
		put64(s + 8, bulk->segments[i].length);
		put32(s + 16, bulk->segments[i].key);
	}
	*used = size;
	return 0;
}

int
crt_native_bulk_unpack(const void *buffer, size_t size, struct crt_native_bulk_remote *out)
{
	const uint8_t                *p      = buffer;
	struct crt_native_bulk_remote remote = {0};

	if (p == NULL || out == NULL || size < CRT_NATIVE_BULK_HEADER)
		return EINVAL;
	if (memcmp(p, "CRTK", 4) != 0 || p[4] != 1 || p[5] == 0 ||
	    (p[5] & ~(CRT_NATIVE_REMOTE_READ | CRT_NATIVE_REMOTE_WRITE)) != 0)
		return EPROTO;
	remote.count    = (unsigned)p[6] << 8 | p[7];
	remote.access   = p[5];
	remote.lease_id = get64(p + 8);
	if (remote.count == 0 || remote.count > CRT_NATIVE_BULK_MAX_SEGMENTS ||
	    remote.lease_id == 0 ||
	    size != CRT_NATIVE_BULK_HEADER + remote.count * CRT_NATIVE_BULK_SEGMENT)
		return EPROTO;
	for (unsigned i = 0; i < remote.count; i++) {
		const uint8_t *s = p + CRT_NATIVE_BULK_HEADER + i * CRT_NATIVE_BULK_SEGMENT;
		struct crt_native_remote_memory *m = &remote.segments[i];
		m->address                         = get64(s);
		m->length                          = get64(s + 8);
		m->key                             = get32(s + 16);
		if (get32(s + 20) != 0 || m->length == 0 || m->length > UINT64_MAX - m->address ||
		    m->length > UINT64_MAX - remote.length)
			return EPROTO;
		remote.length += m->length;
	}
	*out = remote;
	return 0;
}

static int
slice(const struct crt_native_remote_memory *segments, unsigned count, uint64_t total,
      uint64_t offset, uint64_t remaining, unsigned *index, size_t *within, size_t *size)
{
	if (remaining == 0 || offset > total || remaining > total - offset)
		return EINVAL;
	for (unsigned i = 0; i < count; i++) {
		if (offset >= segments[i].length) {
			offset -= segments[i].length;
			continue;
		}
		uint64_t n = segments[i].length - offset;
		if (n > remaining)
			n = remaining;
		if (n > UINT32_MAX)
			n = UINT32_MAX;
		if (offset > SIZE_MAX)
			return EOVERFLOW;
		*index  = i;
		*within = (size_t)offset;
		*size   = (size_t)n;
		return 0;
	}
	return EINVAL;
}

int
crt_native_bulk_local_slice(struct crt_native_bulk *bulk, uint64_t offset, uint64_t remaining,
			    struct crt_native_mr **mr, size_t *within, size_t *size)
{
	unsigned index;
	int      rc;
	if (bulk == NULL || mr == NULL || within == NULL || size == NULL)
		return EINVAL;
	if (bulk->closing)
		return ESHUTDOWN;
	rc = slice(bulk->segments, bulk->count, bulk->length, offset, remaining, &index, within,
		   size);
	if (rc == 0)
		*mr = bulk->mrs[index];
	return rc;
}

int
crt_native_bulk_remote_slice(const struct crt_native_bulk_remote *remote, unsigned access,
			     uint64_t offset, uint64_t remaining,
			     struct crt_native_remote_memory *segment, size_t *within, size_t *size)
{
	unsigned index;
	int      rc;
	if (remote == NULL || segment == NULL || within == NULL || size == NULL || access == 0 ||
	    (access & ~(CRT_NATIVE_REMOTE_READ | CRT_NATIVE_REMOTE_WRITE)) != 0)
		return EINVAL;
	if ((remote->access & access) != access)
		return EACCES;
	if (remote->count == 0 || remote->count > CRT_NATIVE_BULK_MAX_SEGMENTS)
		return EINVAL;
	rc = slice(remote->segments, remote->count, remote->length, offset, remaining, &index,
		   within, size);
	if (rc == 0)
		*segment = remote->segments[index];
	return rc;
}

int
crt_native_bulk_transfer_init(struct crt_native_bulk_transfer *t, const struct crt_native_ops *ops,
			      struct crt_native_ep *ep, struct crt_native_bulk *local,
			      const struct crt_native_bulk_remote *remote,
			      enum crt_native_opcode opcode, uint64_t local_offset,
			      uint64_t remote_offset, uint64_t length)
{
	unsigned local_access, remote_access;
	int      rc;

	if (t == NULL || ops == NULL || ops->post == NULL || ep == NULL || local == NULL ||
	    remote == NULL || remote->lease_id == 0 || length == 0 || local->ep != ep ||
	    local->ops != ops || (opcode != CRT_NATIVE_READ && opcode != CRT_NATIVE_WRITE))
		return EINVAL;
	if (t->local_lease.owner != NULL)
		return EBUSY;
	local_access = opcode == CRT_NATIVE_READ ? CRT_NATIVE_REMOTE_WRITE : CRT_NATIVE_REMOTE_READ;
	remote_access =
	    opcode == CRT_NATIVE_READ ? CRT_NATIVE_REMOTE_READ : CRT_NATIVE_REMOTE_WRITE;
	if ((local->access & local_access) == 0 || (remote->access & remote_access) == 0)
		return EACCES;
	*t = (struct crt_native_bulk_transfer){.ops = ops, .ep = ep, .opcode = opcode};
	rc = crt_native_bulk_acquire(local, remote->lease_id, &t->local_lease);
	if (rc != 0)
		return rc;
	while (length != 0) {
		struct crt_native_bulk_chunk *chunk;
		size_t                        local_size, remote_size;
		if (t->count == CRT_NATIVE_BULK_MAX_CHUNKS) {
			rc = E2BIG;
			goto fail;
		}
		chunk = &t->chunks[t->count];
		rc    = crt_native_bulk_local_slice(local, local_offset, length, &chunk->local,
						    &chunk->local_offset, &local_size);
		if (rc != 0)
			goto fail;
		rc = crt_native_bulk_remote_slice(remote, remote_access, remote_offset, length,
						  &chunk->remote, &chunk->remote_offset,
						  &remote_size);
		if (rc != 0)
			goto fail;
		chunk->size = local_size < remote_size ? local_size : remote_size;
		local_offset += chunk->size;
		remote_offset += chunk->size;
		length -= chunk->size;
		t->count++;
	}
	return 0;
fail:
	crt_native_bulk_release(&t->local_lease);
	*t = (struct crt_native_bulk_transfer){0};
	return rc;
}

int
crt_native_bulk_transfer_post(struct crt_native_bulk_transfer *t)
{
	if (t == NULL || t->local_lease.owner == NULL)
		return EINVAL;
	if (t->error)
		return t->error;
	for (unsigned budget = 0; t->next < t->count && budget < 32; budget++) {
		struct crt_native_bulk_chunk *chunk = &t->chunks[t->next];
		int rc = t->ops->post(t->ep, t->opcode, chunk->local, chunk->local_offset,
				      chunk->size, &chunk->remote, chunk->remote_offset, chunk);
		if (rc == EAGAIN)
			return rc;
		if (rc == EINPROGRESS)
			rc = EIO;
		if (rc != 0) {
			t->error = rc;
			t->next  = t->count;
			return rc;
		}
		chunk->state = 1;
		t->pending++;
		t->next++;
	}
	return 0;
}

int
crt_native_bulk_transfer_complete(struct crt_native_bulk_transfer    *t,
				  const struct crt_native_completion *completion)
{
	uintptr_t                     cookie, begin, end;
	struct crt_native_bulk_chunk *chunk;
	int                           error;

	if (t == NULL || t->local_lease.owner == NULL || completion == NULL)
		return EINVAL;
	cookie = (uintptr_t)completion->cookie;
	begin  = (uintptr_t)&t->chunks[0];
	end    = (uintptr_t)&t->chunks[t->count];
	if (cookie < begin || cookie >= end || (cookie - begin) % sizeof(t->chunks[0]) != 0)
		return EINVAL;
	chunk = completion->cookie;
	if (chunk->state != 1 || completion->opcode != t->opcode)
		return EPROTO;
	chunk->state = 2;
	t->pending--;
	error = completion->error;
	if (error == EINPROGRESS)
		error = EIO;
	if (error == 0 && completion->bytes != chunk->size)
		error = EIO;
	if (error != 0 && t->error == 0) {
		t->error = error;
		t->next  = t->count;
	}
	return 0;
}

int
crt_native_bulk_transfer_cancel(struct crt_native_bulk_transfer *t)
{
	if (t == NULL || t->local_lease.owner == NULL)
		return EINVAL;
	if (t->error == 0)
		t->error = ECANCELED;
	t->next = t->count;
	return 0;
}

int
crt_native_bulk_transfer_status(const struct crt_native_bulk_transfer *t)
{
	if (t == NULL || t->local_lease.owner == NULL)
		return EINVAL;
	if (t->pending != 0 || t->next != t->count)
		return EINPROGRESS;
	return t->error;
}

int
crt_native_bulk_transfer_fini(struct crt_native_bulk_transfer *t)
{
	if (t == NULL || t->local_lease.owner == NULL)
		return EINVAL;
	if (t->pending != 0 || t->next != t->count)
		return EBUSY;
	crt_native_bulk_release(&t->local_lease);
	*t = (struct crt_native_bulk_transfer){0};
	return 0;
}
