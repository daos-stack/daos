/*
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#include "../bulk_session.h"
#include "../rpc.h"
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>

struct crt_native_ep {
	unsigned registered, pending;
	int      error;
	bool     closing, quiesced;
};
struct crt_native_mr {
	struct crt_native_ep *ep;
};
static int
reg(struct crt_native_ep *ep, void *data, size_t size, unsigned access, struct crt_native_mr **mr,
    struct crt_native_remote_memory *remote)
{
	(void)access;
	*mr = calloc(1, sizeof(**mr));
	assert(*mr != NULL);
	(*mr)->ep = ep;
	ep->registered++;
	*remote = (struct crt_native_remote_memory){(uintptr_t)data, size, 7};
	return 0;
}
static int
dereg(struct crt_native_mr *mr)
{
	mr->ep->registered--;
	free(mr);
	return 0;
}
static int
stop(struct crt_native_ep *ep)
{
	ep->closing = true;
	return 0;
}
static int
quiesce(struct crt_native_ep *ep)
{
	assert(ep->closing);
	if (ep->pending)
		return EBUSY;
	if (ep->error)
		return ep->error;
	ep->quiesced = true;
	return 0;
}
static const struct crt_native_ops ops = {
    .register_memory = reg, .deregister_memory = dereg, .close_begin = stop, .quiesce = quiesce};
static void
unexpected(void *arg, struct crt_native_rpc *rpc, struct crt_native_token token, uint32_t opcode,
	   const void *data, size_t size)
{
	(void)arg;
	(void)rpc;
	(void)token;
	(void)opcode;
	(void)data;
	(void)size;
	assert(0);
}
struct sender {
	struct crt_native_rpc *rpc;
	unsigned               blocked;
};
static int
send_ack(void *arg, uint64_t id)
{
	struct sender *sender = arg;
	if (sender->blocked) {
		sender->blocked--;
		return EAGAIN;
	}
	return crt_native_rpc_queue_release(sender->rpc, id);
}
static void
hold(void *arg)
{
	++*(unsigned *)arg;
}
static void
drop(void *arg)
{
	assert(*(unsigned *)arg);
	--*(unsigned *)arg;
}

int
main(void)
{
	struct crt_native_ep                 a = {0}, b = {0}, other = {0};
	struct crt_native_bulk_session      *sa, *sb;
	struct crt_native_rpc               *ra, *rb;
	struct sender                        sender_a = {0}, sender_b = {0};
	struct crt_native_bulk              *bulk, *wrong;
	char                                 data[64];
	struct crt_native_iov                iov = {data, sizeof(data)};
	struct crt_native_bulk_remote        remote;
	const struct crt_native_bulk_remote *borrowed;
	uint8_t                              descriptor[128];
	size_t                               size;
	uint64_t                             id, newer, saved;
	unsigned                             refs  = 0;
	struct crt_native_bulk_owner         owner = {&refs, hold, drop};
	assert(crt_native_rpc_create(1, 64, unexpected, NULL, &ra) == 0);
	assert(crt_native_rpc_create(1, 64, unexpected, NULL, &rb) == 0);
	sender_a.rpc = ra;
	sender_b.rpc = rb;
	assert(crt_native_bulk_session_create(&ops, &a, 2, send_ack, &sender_a, &sa) == 0);
	assert(crt_native_bulk_session_create(&ops, &b, 2, send_ack, &sender_b, &sb) == 0);
	assert(crt_native_rpc_set_release_handler(ra, crt_native_bulk_session_ack, sa) == 0);
	assert(crt_native_bulk_create(&ops, &a, &iov, 1, CRT_NATIVE_REMOTE_READ, &bulk) == 0);
	assert(crt_native_bulk_create(&ops, &other, &iov, 1, CRT_NATIVE_REMOTE_READ, &wrong) == 0);
	assert(crt_native_bulk_export_begin(sa, wrong, NULL, &id) == EXDEV);
	assert(crt_native_bulk_destroy(wrong) == 0);
	assert(crt_native_bulk_export_begin(sa, bulk, &owner, &id) == 0 && refs == 1);
	assert(crt_native_bulk_session_ack(sa, id) == EPROTO); /* draft not published */
	assert(crt_native_bulk_export_abort(sa, id) == 0 && refs == 0);
	assert(crt_native_bulk_export_begin(sa, bulk, &owner, &id) == 0);
	assert(crt_native_bulk_export_pack(sa, id, descriptor, sizeof(descriptor), &size) == 0);
	assert(crt_native_bulk_unpack(descriptor, size, &remote) == 0);
	assert(crt_native_bulk_export_publish(sa, id) == 0);
	assert(crt_native_bulk_export_abort(sa, id) == EBUSY);
	assert(crt_native_bulk_destroy(bulk) == EBUSY);
	assert(crt_native_bulk_import(sb, &remote) == 0);
	assert(crt_native_bulk_import_get(sb, id, &borrowed) == 0 && borrowed->lease_id == id);
	assert(crt_native_bulk_import_put(sb, id) ==
	       0); /* wrapper released, transfer still owns key */
	assert(crt_native_bulk_session_progress(sb) == 0);
	struct crt_native_tx tx;
	assert(crt_native_rpc_next_tx(rb, &tx) == EAGAIN);
	assert(crt_native_bulk_import_put(sb, id) == 0); /* accepted transfer completed */
	assert(crt_native_bulk_import_put(sb, id) == ENOENT);
	sender_b.blocked = 1;
	assert(crt_native_bulk_session_progress(sb) == EAGAIN);
	assert(crt_native_bulk_session_destroy(sb) == EBUSY);
	assert(crt_native_bulk_session_progress(sb) == 0);
	assert(crt_native_rpc_next_tx(rb, &tx) == 0 && tx.token.control);
	assert(crt_native_bulk_destroy(bulk) == EBUSY); /* queue/SEND isn't peer ACK receipt */
	assert(crt_native_rpc_receive(ra, tx.data, tx.length) == 0 && refs == 0);
	assert(crt_native_rpc_tx_complete(rb, tx.token, 0) == 0);
	assert(crt_native_bulk_session_ack(sa, id) == 0); /* duplicate */
	saved = id;
	assert(crt_native_bulk_export_begin(sa, bulk, &owner, &newer) == 0 && newer > id);
	assert(crt_native_bulk_export_publish(sa, newer) == 0);
	assert(crt_native_bulk_session_ack(sa, saved) == 0 && refs == 1); /* stale */
	assert(crt_native_bulk_session_ack(sa, newer + (UINT64_C(1) << 32)) == EPROTO);
	assert(crt_native_bulk_export_begin(sa, bulk, NULL, &id) == 0);
	uint64_t ignored;
	assert(crt_native_bulk_export_begin(sa, bulk, NULL, &ignored) == EAGAIN);
	assert(crt_native_bulk_session_shutdown(sa) == 0);
	a.pending = 1;
	assert(crt_native_bulk_session_quiesce(sa) == EBUSY && refs == 1);
	a.pending = 0;
	a.error   = EIO;
	assert(crt_native_bulk_session_quiesce(sa) == EIO && refs == 1);
	assert(crt_native_bulk_destroy(bulk) == EBUSY);
	a.error = 0;
	assert(crt_native_bulk_session_quiesce(sa) == 0 && a.quiesced && refs == 0);
	assert(crt_native_bulk_session_quiesce(sa) == 0);
	assert(crt_native_bulk_destroy(bulk) == 0 && a.registered == 0);
	assert(crt_native_bulk_session_destroy(sa) == 0);
	/* An imported wrapper also pins session memory across disconnect. */
	remote.lease_id = newer;
	assert(crt_native_bulk_import(sb, &remote) == 0);
	assert(crt_native_bulk_session_shutdown(sb) == 0);
	assert(crt_native_bulk_session_quiesce(sb) == 0);
	assert(crt_native_bulk_session_destroy(sb) == EBUSY);
	assert(crt_native_bulk_import_put(sb, newer) == 0);
	assert(crt_native_bulk_session_destroy(sb) == 0);
	assert(crt_native_rpc_destroy(ra) == 0 && crt_native_rpc_destroy(rb) == 0);
	puts("Bulk session: publish/abort, key acknowledgments, transfer pins, stale generations, "
	     "backpressure and remote quiescence passed");
	return 0;
}
