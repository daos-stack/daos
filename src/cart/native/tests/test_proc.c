/*
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#include "../proc.h"
#include "../rpc.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

static void
scalars(void)
{
	uint8_t       storage[64], *buffer = storage + 1; /* deliberately unaligned */
	const uint8_t expected[] = {'C',  'R',  'T',  'P', 1, 0, 0, 0, 0xab, 0x12, 0x34, 0x89,
				    0xab, 0xcd, 0xef, 1,   2, 3, 4, 5, 6,    7,    8,    1};
	struct crt_native_proc p;
	uint8_t                a = 0xab;
	uint16_t               b = 0x1234;
	uint32_t               c = 0x89abcdef;
	uint64_t               d = UINT64_C(0x0102030405060708);
	bool                   e = true;
	size_t                 size, used;

	assert(crt_native_proc_init(&p, CRT_NATIVE_ENCODE, buffer, 63) == 0);
	assert(crt_native_proc_u8(&p, &a) == 0);
	assert(crt_native_proc_u16(&p, &b) == 0);
	assert(crt_native_proc_u32(&p, &c) == 0);
	assert(crt_native_proc_u64(&p, &d) == 0);
	assert(crt_native_proc_bool(&p, &e) == 0);
	assert(crt_native_proc_finish(&p, &size) == 0 && size == sizeof(expected));
	assert(memcmp(buffer, expected, size) == 0);
	for (size_t n = 0; n <= size; n++) {
		int rc = crt_native_proc_init(&p, CRT_NATIVE_DECODE, buffer, n);
		a      = 0;
		b      = 0;
		c      = 0;
		d      = 0;
		e      = false;
		if (!rc)
			rc = crt_native_proc_u8(&p, &a);
		if (!rc)
			rc = crt_native_proc_u16(&p, &b);
		if (!rc)
			rc = crt_native_proc_u32(&p, &c);
		if (!rc)
			rc = crt_native_proc_u64(&p, &d);
		if (!rc)
			rc = crt_native_proc_bool(&p, &e);
		if (!rc)
			rc = crt_native_proc_finish(&p, &used);
		assert(rc == (n == size ? 0 : EPROTO));
	}
	assert(a == 0xab && b == 0x1234 && c == 0x89abcdef && d == UINT64_C(0x0102030405060708) &&
	       e);
	buffer[size - 1] = 2;
	assert(crt_native_proc_init(&p, CRT_NATIVE_DECODE, buffer, size) == 0);
	void *view;
	assert(crt_native_proc_span(&p, size - CRT_NATIVE_PROC_HEADER - 1, &view) == 0);
	assert(crt_native_proc_bool(&p, &e) == EPROTO);
	assert(crt_native_proc_finish(&p, &used) == EPROTO);
	buffer[4] = 2;
	assert(crt_native_proc_init(&p, CRT_NATIVE_DECODE, buffer, size) == EPROTO);
	assert(crt_native_proc_init(&p, CRT_NATIVE_ENCODE, buffer, 8) == 0);
	assert(crt_native_proc_u8(&p, &a) == EMSGSIZE);
	assert(crt_native_proc_raw(&p, NULL, 1) == EMSGSIZE); /* sticky error */
}

static void
signed_and_direct_span(void)
{
	struct crt_native_proc p;
	uint8_t                buffer[64];
	int8_t                 a      = INT8_MIN;
	int16_t                b      = INT16_MIN;
	int32_t                c      = INT32_MIN;
	int64_t                d      = INT64_MIN;
	uint64_t               length = 7;
	void                  *view;
	size_t                 used;
	assert(crt_native_proc_init(&p, CRT_NATIVE_ENCODE, buffer, sizeof(buffer)) == 0);
	assert(crt_native_proc_i8(&p, &a) == 0);
	assert(crt_native_proc_i16(&p, &b) == 0);
	assert(crt_native_proc_i32(&p, &c) == 0);
	assert(crt_native_proc_i64(&p, &d) == 0);
	assert(crt_native_proc_u64(&p, &length) == 0);
	assert(crt_native_proc_span(&p, 7, &view) == 0 && view == buffer + 31);
	/* Producer writes into final buffer directly: no staging buffer/copy. */
	for (unsigned i = 0; i < 7; i++)
		((uint8_t *)view)[i] = (uint8_t)i;
	assert(crt_native_proc_finish(&p, &used) == 0);
	assert(buffer[8] == 0x80 && buffer[9] == 0x80 && buffer[11] == 0x80 && buffer[15] == 0x80);
	assert(crt_native_proc_init(&p, CRT_NATIVE_DECODE, buffer, used) == 0);
	a = b = c = d = 0;
	assert(crt_native_proc_i8(&p, &a) == 0 && a == INT8_MIN);
	assert(crt_native_proc_i16(&p, &b) == 0 && b == INT16_MIN);
	assert(crt_native_proc_i32(&p, &c) == 0 && c == INT32_MIN);
	assert(crt_native_proc_i64(&p, &d) == 0 && d == INT64_MIN);
	size_t size = 0;
	assert(crt_native_proc_bytes(&p, &view, &size, 7) == 0);
	assert(view == buffer + 31 && size == 7);
	for (unsigned i = 0; i < 7; i++)
		assert(((uint8_t *)view)[i] == i);
	assert(crt_native_proc_finish(&p, &used) == 0);
}

static void
borrowed_views(void)
{
	struct crt_native_proc p;
	uint8_t                buffer[128];
	void                  *data = (void *)"abc", *view;
	char                  *s = "hello", *null_string = NULL, *empty = "";
	size_t                 length = 3, used;

	assert(crt_native_proc_init(&p, CRT_NATIVE_ENCODE, buffer, sizeof(buffer)) == 0);
	assert(crt_native_proc_bytes(&p, &data, &length, 3) == 0);
	assert(crt_native_proc_string(&p, &s, 6) == 0);
	assert(crt_native_proc_string(&p, &null_string, 6) == 0);
	assert(crt_native_proc_string(&p, &empty, 1) == 0);
	assert(crt_native_proc_finish(&p, &used) == 0);
	size_t size = used;
	assert(crt_native_proc_init(&p, CRT_NATIVE_DECODE, buffer, size) == 0);
	data   = NULL;
	length = 0;
	s      = NULL;
	empty  = NULL;
	assert(crt_native_proc_bytes(&p, &data, &length, 3) == 0);
	assert(data == buffer + 16 && length == 3 && memcmp(data, "abc", 3) == 0);
	assert(crt_native_proc_string(&p, &s, 6) == 0);
	assert(s == (char *)buffer + 27 && strcmp(s, "hello") == 0);
	assert(crt_native_proc_string(&p, &null_string, 6) == 0 && null_string == NULL);
	assert(crt_native_proc_string(&p, &empty, 1) == 0 && empty != NULL && *empty == 0);
	assert(crt_native_proc_finish(&p, &used) == 0);
	assert(crt_native_proc_init(&p, CRT_NATIVE_FREE, NULL, 0) == 0);
	assert(crt_native_proc_bytes(&p, &data, &length, 3) == 0 && data == NULL && length == 0);
	assert(crt_native_proc_string(&p, &s, 6) == 0 && s == NULL);
	assert(memcmp(buffer + 27, "hello", 6) == 0); /* FREE didn't touch borrowed memory */
	assert(crt_native_proc_init(&p, CRT_NATIVE_DECODE, buffer, size + 1) == 0);
	assert(crt_native_proc_span(&p, size - 8, &view) == 0);
	assert(crt_native_proc_finish(&p, &used) == EPROTO); /* trailing data */
	memset(buffer + 8, 0xff, 8);                         /* impossible u64 byte count */
	assert(crt_native_proc_init(&p, CRT_NATIVE_DECODE, buffer, size) == 0);
	assert(crt_native_proc_bytes(&p, &data, &length, SIZE_MAX) == EPROTO);
	assert(crt_native_proc_init(&p, CRT_NATIVE_ENCODE, buffer, sizeof(buffer)) == 0);
	s = "abc";
	assert(crt_native_proc_string(&p, &s, 4) == 0);
	buffer[17] = 0; /* embedded NUL */
	assert(crt_native_proc_init(&p, CRT_NATIVE_DECODE, buffer, 20) == 0);
	assert(crt_native_proc_string(&p, &s, 4) == EPROTO);
}

static void
bulk_descriptor(void)
{
	/* Test codec framing with a canonical remote descriptor. Registration and
	 * lease pinning themselves are exercised by test_bulk.
	 */
	uint8_t                       buffer[64] = {'C', 'R', 'T', 'P', 1,   0,   0,   0,    0,
						    0,   0,   40,  'C', 'R', 'T', 'K', 1,    CRT_NATIVE_REMOTE_READ,
						    0,   1,   0,   0,   0,   0,   0,   0,    0,
						    7,   0,   0,   0,   0,   0,   0,   0x10, 0,
						    0,   0,   0,   0,   0,   0,   0,   16,   0,
						    0,   0,   9,   0,   0,   0,   0};
	struct crt_native_proc        p;
	struct crt_native_bulk_remote remote;
	size_t                        used;
	assert(crt_native_proc_init(&p, CRT_NATIVE_DECODE, buffer, 52) == 0);
	assert(crt_native_proc_bulk(&p, NULL, &remote) == 0);
	assert(remote.lease_id == 7 && remote.length == 16 && remote.count == 1 &&
	       remote.segments[0].address == 0x1000 && remote.segments[0].key == 9);
	assert(crt_native_proc_finish(&p, &used) == 0);
	for (size_t n = 8; n < 52; n++) {
		assert(crt_native_proc_init(&p, CRT_NATIVE_DECODE, buffer, n) == 0);
		assert(crt_native_proc_bulk(&p, NULL, &remote) == EPROTO);
	}
	assert(crt_native_proc_init(&p, CRT_NATIVE_FREE, NULL, 0) == 0);
	assert(crt_native_proc_bulk(&p, NULL, &remote) == 0 && remote.count == 0);
	assert(crt_native_proc_init(&p, CRT_NATIVE_ENCODE, buffer, sizeof(buffer)) == 0);
	assert(crt_native_proc_bulk(&p, NULL, NULL) == 0);
	assert(crt_native_proc_finish(&p, &used) == 0 && used == 12);
	assert(crt_native_proc_init(&p, CRT_NATIVE_DECODE, buffer, used) == 0);
	assert(crt_native_proc_bulk(&p, NULL, &remote) == 0 && remote.lease_id == 0);
}

struct message {
	uint64_t number;
	void    *data;
	size_t   size;
};
static int
message(struct crt_native_proc *p, struct message *m)
{
	int rc = crt_native_proc_u64(p, &m->number);
	return rc ? rc : crt_native_proc_bytes(p, &m->data, &m->size, 32);
}

struct encoding {
	struct crt_native_rpc *rpc;
	void                  *address;
	int                    error;
};
struct completion_result {
	unsigned    count;
	const void *received;
};
static int
encode(void *arg, void *buffer, size_t capacity, size_t *used)
{
	struct encoding       *e = arg;
	struct crt_native_proc p;
	struct message         m = {.number = 42, .data = (void *)"payload", .size = 7};
	struct crt_native_tx   tx;
	e->address = buffer;
	assert(crt_native_rpc_next_tx(e->rpc, &tx) == EBUSY);
	assert(crt_native_rpc_destroy(e->rpc) == EBUSY);
	if (e->error == EMSGSIZE) {
		*used = capacity + 1;
		return 0;
	}
	if (e->error)
		return e->error;
	int rc = crt_native_proc_init(&p, CRT_NATIVE_ENCODE, buffer, capacity);
	if (!rc)
		rc = message(&p, &m);
	return rc ? rc : crt_native_proc_finish(&p, used);
}

static void
completed(void *arg, int error, uint32_t status, const void *payload, size_t size)
{
	struct completion_result *result = arg;
	struct crt_native_proc    p;
	struct message            m = {0};
	size_t                    used;
	assert(error == 0 && status == 0);
	assert(crt_native_proc_init(&p, CRT_NATIVE_DECODE, (void *)payload, size) == 0);
	assert(message(&p, &m) == 0);
	assert(crt_native_proc_finish(&p, &used) == 0);
	assert(m.number == 42 && m.size == 7 && m.data == (const uint8_t *)payload + 24);
	assert(memcmp(m.data, "payload", 7) == 0);
	result->received = payload;
	result->count++;
}

static void
handler(void *arg, struct crt_native_rpc *rpc, struct crt_native_token token, uint32_t opcode,
	const void *payload, size_t size)
{
	struct encoding       *e = arg;
	struct crt_native_proc p;
	struct message         m = {0};
	size_t                 used;
	assert(opcode == 17);
	assert(crt_native_proc_init(&p, CRT_NATIVE_DECODE, (void *)payload, size) == 0);
	assert(message(&p, &m) == 0 && crt_native_proc_finish(&p, &used) == 0);
	assert(m.data == (const uint8_t *)payload + 24 && m.number == 42);
	e->error = EMSGSIZE;
	assert(crt_native_rpc_reply_encoded(rpc, token, 0, encode, e) == EMSGSIZE);
	e->error = 0;
	assert(crt_native_rpc_reply_encoded(rpc, token, 0, encode, e) == 0);
}

static void
direct_send(void)
{
	struct crt_native_rpc   *a, *b;
	struct encoding          ea = {0}, eb = {0};
	struct crt_native_tx     request, reply;
	struct completion_result result = {0};
	uint64_t                 id     = 0;
	assert(crt_native_rpc_create(1, 128, handler, &ea, &a) == 0);
	assert(crt_native_rpc_create(1, 128, handler, &eb, &b) == 0);
	ea.rpc = a;
	eb.rpc = b;
	for (unsigned i = 0; i < 1024; i++) {
		ea.error = EINVAL;
		assert(crt_native_rpc_call_encoded(a, 17, encode, &ea, completed, &result, &id) ==
		       EINVAL);
		assert(crt_native_rpc_idle(a));
		assert(crt_native_rpc_next_tx(a, &request) == EAGAIN);
		ea.error = 0;
		assert(crt_native_rpc_call_encoded(a, 17, encode, &ea, completed, &result, &id) ==
		       0);
		assert(crt_native_rpc_next_tx(a, &request) == 0);
		assert((const uint8_t *)request.data + CRT_NATIVE_HEADER_SIZE == ea.address);
		assert(crt_native_rpc_receive(b, request.data, request.length) == 0);
		assert(crt_native_rpc_next_tx(b, &reply) == 0);
		assert((const uint8_t *)reply.data + CRT_NATIVE_HEADER_SIZE == eb.address);
		if (i % 2) {
			assert(crt_native_rpc_tx_complete(a, request.token, 0) == 0);
			assert(crt_native_rpc_receive(a, reply.data, reply.length) == 0);
			assert(result.received ==
			       (const uint8_t *)reply.data + CRT_NATIVE_HEADER_SIZE);
		} else {
			assert(crt_native_rpc_receive(a, reply.data, reply.length) == 0);
			assert(result.count == i);
			assert(crt_native_rpc_tx_complete(a, request.token, 0) == 0);
			assert(result.received !=
			       (const uint8_t *)reply.data + CRT_NATIVE_HEADER_SIZE);
		}
		assert(result.count == i + 1);
		assert(crt_native_rpc_tx_complete(b, reply.token, 0) == 0);
	}
	assert(crt_native_rpc_destroy(a) == 0 && crt_native_rpc_destroy(b) == 0);
}

int
main(void)
{
	scalars();
	signed_and_direct_span();
	borrowed_views();
	bulk_descriptor();
	direct_send();
	puts("Native payload codec: canonical scalars, bounds, borrowed views, bulk descriptors "
	     "and direct SEND passed");
	return 0;
}
