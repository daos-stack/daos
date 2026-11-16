/*
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#include "../bulk.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct crt_native_ep {
	unsigned calls, fail_register, fail_deregister, active, access, block_post;
};
struct crt_native_mr {
	struct crt_native_ep *owner;
	uint8_t              *base;
	size_t                size;
};
static struct crt_native_completion completions[128];
static unsigned                     num_completions;
static int
reg(struct crt_native_ep *ep, void *base, size_t size, unsigned access, struct crt_native_mr **mr,
    struct crt_native_remote_memory *remote)
{
	*mr = NULL;
	if (++ep->calls == ep->fail_register)
		return ENOMEM;
	*mr = calloc(1, sizeof(**mr));
	assert(*mr != NULL);
	(*mr)->owner = ep;
	(*mr)->base  = base;
	(*mr)->size  = size;
	ep->active++;
	ep->access = access;
	*remote    = (struct crt_native_remote_memory){(uintptr_t)base, size, 0x12345678};
	return 0;
}
static int
dereg(struct crt_native_mr *mr)
{
	if (mr->owner->fail_deregister != 0) {
		mr->owner->fail_deregister--;
		return EBUSY;
	}
	mr->owner->active--;
	free(mr);
	return 0;
}
static int
post(struct crt_native_ep *ep, enum crt_native_opcode opcode, struct crt_native_mr *mr,
     size_t offset, size_t size, const struct crt_native_remote_memory *remote,
     size_t remote_offset, void *cookie)
{
	if (ep->block_post) {
		ep->block_post--;
		return EAGAIN;
	}
	assert(offset <= mr->size && size <= mr->size - offset);
	assert(remote_offset <= remote->length && size <= remote->length - remote_offset);
	if (opcode == CRT_NATIVE_READ)
		memcpy(mr->base + offset, (void *)(uintptr_t)(remote->address + remote_offset),
		       size);
	else {
		assert(opcode == CRT_NATIVE_WRITE);
		memcpy((void *)(uintptr_t)(remote->address + remote_offset), mr->base + offset,
		       size);
	}
	completions[num_completions++] = (struct crt_native_completion){cookie, opcode, 0, size};
	return 0;
}
static const struct crt_native_ops ops = {
    .register_memory = reg, .deregister_memory = dereg, .post = post};

static void
test_transfer(void)
{
	struct crt_native_ep          ep    = {0};
	char                          a[32] = {0}, b[64] = {0}, source[96];
	struct crt_native_iov         iov[] = {{a, sizeof(a)}, {b, sizeof(b)}};
	struct crt_native_bulk       *local;
	struct crt_native_bulk_remote remote = {
	    .lease_id = 7, .length = 96, .count = 2, .access = CRT_NATIVE_REMOTE_READ};
	struct crt_native_bulk_transfer transfer = {0};
	memset(source, 0x42, sizeof(source));
	remote.segments[0] = (struct crt_native_remote_memory){(uintptr_t)source, 16, 1};
	remote.segments[1] = (struct crt_native_remote_memory){(uintptr_t)(source + 16), 80, 2};
	assert(crt_native_bulk_create(&ops, &ep, iov, 2,
				      CRT_NATIVE_REMOTE_READ | CRT_NATIVE_REMOTE_WRITE,
				      &local) == 0);
	assert(crt_native_bulk_transfer_init(&transfer, &ops, &ep, local, &remote, CRT_NATIVE_WRITE,
					     0, 0, 80) == EACCES);
	assert(crt_native_bulk_transfer_init(&transfer, &ops, &ep, local, &remote, CRT_NATIVE_READ,
					     0, 0, 80) == 0);
	assert(transfer.count == 3);
	ep.block_post = 1;
	assert(crt_native_bulk_transfer_post(&transfer) == EAGAIN);
	assert(crt_native_bulk_transfer_status(&transfer) == EINPROGRESS);
	assert(crt_native_bulk_transfer_post(&transfer) == 0);
	assert(crt_native_bulk_destroy(local) == EBUSY);
	assert(crt_native_bulk_transfer_fini(&transfer) == EBUSY);
	assert(crt_native_bulk_transfer_cancel(&transfer) == 0);
	assert(crt_native_bulk_transfer_status(&transfer) == EINPROGRESS);
	while (num_completions)
		assert(crt_native_bulk_transfer_complete(&transfer,
							 &completions[--num_completions]) == 0);
	assert(crt_native_bulk_transfer_status(&transfer) == ECANCELED);
	assert(crt_native_bulk_transfer_fini(&transfer) == 0);
	assert(memcmp(a, source, 32) == 0 && memcmp(b, source + 32, 48) == 0);
	/* Failed accepted operations must also release the lease after draining. */
	assert(crt_native_bulk_transfer_init(&transfer, &ops, &ep, local, &remote, CRT_NATIVE_READ,
					     0, 0, 80) == 0);
	assert(crt_native_bulk_transfer_post(&transfer) == 0);
	completions[0].error = EINVAL;
	while (num_completions)
		assert(crt_native_bulk_transfer_complete(&transfer,
							 &completions[--num_completions]) == 0);
	assert(crt_native_bulk_transfer_status(&transfer) == EINVAL);
	assert(crt_native_bulk_transfer_fini(&transfer) == 0);
	assert(crt_native_bulk_destroy(local) == 0);
}

int
main(void)
{
	test_transfer();
	struct crt_native_ep            ep = {0};
	struct crt_native_bulk         *bulk;
	struct crt_native_bulk_lease    lease = {0};
	struct crt_native_bulk_remote   remote;
	struct crt_native_remote_memory segment;
	struct crt_native_mr           *mr;
	char                            a[32], b[64];
	struct crt_native_iov           iov[] = {{a, sizeof(a)}, {b, sizeof(b)}};
	uint8_t                         frame[CRT_NATIVE_BULK_HEADER + 2 * CRT_NATIVE_BULK_SEGMENT];
	size_t                          used, offset, size;

	assert(crt_native_bulk_create(&ops, &ep, iov, 2, CRT_NATIVE_REMOTE_READ, &bulk) == 0);
	assert(ep.active == 2 && ep.access == CRT_NATIVE_REMOTE_READ);
	assert(crt_native_bulk_acquire(bulk, UINT64_C(0x1122334455667788), &lease) == 0);
	assert(crt_native_bulk_acquire(bulk, 2, &lease) == EBUSY);
	assert(crt_native_bulk_destroy(bulk) == EBUSY);
	assert(crt_native_bulk_pack(&lease, frame, sizeof(frame) - 1, &used) == EMSGSIZE);
	assert(crt_native_bulk_pack(&lease, frame, sizeof(frame), &used) == 0 &&
	       used == sizeof(frame));
	assert(frame[8] == 0x11 && frame[15] == 0x88);
	assert(crt_native_bulk_unpack(frame, used, &remote) == 0);
	assert(remote.lease_id == lease.id && remote.length == 96 && remote.count == 2);
	assert(remote.segments[0].key == 0x12345678 && remote.segments[1].address == (uintptr_t)b);
	assert(crt_native_bulk_remote_slice(&remote, CRT_NATIVE_REMOTE_WRITE, 0, 16, &segment,
					    &offset, &size) == EACCES);
	assert(crt_native_bulk_remote_slice(&remote, CRT_NATIVE_REMOTE_READ, 16, 64, &segment,
					    &offset, &size) == 0 &&
	       offset == 16 && size == 16);
	assert(crt_native_bulk_local_slice(bulk, 32, 64, &mr, &offset, &size) == 0 && offset == 0 &&
	       size == 64 && mr != NULL);
	assert(crt_native_bulk_local_slice(bulk, 95, 2, &mr, &offset, &size) == EINVAL);
	for (size_t n = 0; n < used; n++)
		assert(crt_native_bulk_unpack(frame, n, &remote) != 0);
	frame[CRT_NATIVE_BULK_HEADER + 20] = 1;
	assert(crt_native_bulk_unpack(frame, used, &remote) == EPROTO);
	frame[CRT_NATIVE_BULK_HEADER + 20] = 0;
	memset(frame + CRT_NATIVE_BULK_HEADER, 0xff, 16);
	assert(crt_native_bulk_unpack(frame, used, &remote) == EPROTO);
	assert(crt_native_bulk_release(&lease) == 0);
	assert(crt_native_bulk_release(&lease) == EINVAL);
	/* A failed deregistration must retain the partially closed handle. */
	ep.fail_deregister = 1;
	assert(crt_native_bulk_destroy(bulk) == EBUSY);
	assert(crt_native_bulk_acquire(bulk, 3, &lease) == ESHUTDOWN);
	assert(crt_native_bulk_destroy(bulk) == 0 && ep.active == 0);
	/* Registration rollback may itself fail; return an owned handle to retry. */
	ep.calls           = 0;
	ep.fail_register   = 2;
	ep.fail_deregister = 1;
	assert(crt_native_bulk_create(&ops, &ep, iov, 2, CRT_NATIVE_REMOTE_READ, &bulk) == ENOMEM);
	assert(bulk != NULL && ep.active == 1);
	assert(crt_native_bulk_destroy(bulk) == 0 && ep.active == 0);
	puts("bulk: wire validation, access, SGL bounds, export leases and rollback tests passed");
	return 0;
}
