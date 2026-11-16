/*
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#include "../cart_proc.h"
#include <assert.h>
#include <stdio.h>
#include <stdatomic.h>
#include <pthread.h>

/* Link-time Mercury test double. Real cart/api.h and types are used. */
static atomic_uint fallbacks;
#define FALLBACK(type)                                                                             \
	int crt_hg_proc_##type(crt_proc_t p, crt_proc_op_t op, type *v)                            \
	{                                                                                          \
		(void)p;                                                                           \
		(void)op;                                                                          \
		(void)v;                                                                           \
		atomic_fetch_add(&fallbacks, 1);                                                   \
		return 123;                                                                        \
	}
FALLBACK(int8_t)
FALLBACK(uint8_t)
FALLBACK(int16_t)
FALLBACK(uint16_t)
FALLBACK(int32_t)
FALLBACK(uint32_t)
FALLBACK(int64_t)
FALLBACK(uint64_t)
FALLBACK(bool)
FALLBACK(crt_bulk_t)
FALLBACK(d_string_t)
FALLBACK(d_const_string_t)
FALLBACK(d_iov_t)
FALLBACK(d_sg_list_t)
int
crt_hg_proc_d_rank_list_t(crt_proc_t p, crt_proc_op_t op, d_rank_list_t **v)
{
	(void)p;
	(void)op;
	(void)v;
	atomic_fetch_add(&fallbacks, 1);
	return 123;
}
int
crt_hg_proc_get_op(crt_proc_t p, crt_proc_op_t *op)
{
	(void)p;
	(void)op;
	atomic_fetch_add(&fallbacks, 1);
	return 123;
}
int
crt_hg_proc_memcpy(crt_proc_t p, crt_proc_op_t op, void *v, size_t size)
{
	(void)p;
	(void)op;
	(void)v;
	(void)size;
	atomic_fetch_add(&fallbacks, 1);
	return 123;
}

struct message {
	uint32_t         number;
	d_string_t       string;
	d_const_string_t const_string;
	d_iov_t          iov;
	d_rank_list_t   *ranks;
	d_sg_list_t      sgl;
	crt_bulk_t       bulk;
};

static int
message(crt_proc_t proc, void *arg)
{
	struct message *m = arg;
	crt_proc_op_t   op;
	int             rc = crt_proc_get_op(proc, &op);
	if (!rc)
		rc = crt_proc_uint32_t(proc, op, &m->number);
	if (!rc)
		rc = crt_proc_d_string_t(proc, op, &m->string);
	if (!rc)
		rc = crt_proc_d_const_string_t(proc, op, &m->const_string);
	if (!rc)
		rc = crt_proc_d_iov_t(proc, op, &m->iov);
	if (!rc)
		rc = crt_proc_d_rank_list_t(proc, op, &m->ranks);
	if (!rc)
		rc = crt_proc_d_sg_list_t(proc, op, &m->sgl);
	if (!rc)
		rc = crt_proc_crt_bulk_t(proc, op, &m->bulk);
	/* Match the generated callbacks' same-proc error cleanup convention. */
	if (rc) {
		assert(crt_proc_d_string_t(proc, CRT_PROC_FREE, &m->string) == 0);
		assert(crt_proc_d_const_string_t(proc, CRT_PROC_FREE, &m->const_string) == 0);
		assert(crt_proc_d_iov_t(proc, CRT_PROC_FREE, &m->iov) == 0);
		assert(crt_proc_d_rank_list_t(proc, CRT_PROC_FREE, &m->ranks) == 0);
		assert(crt_proc_d_sg_list_t(proc, CRT_PROC_FREE, &m->sgl) == 0);
	}
	return rc;
}

static void
roundtrip(void)
{
	uint8_t        frame[512];
	d_rank_t       ranks[] = {1, 2, 0xabcdef};
	d_rank_list_t  list    = {.rl_nr = 3, .rl_ranks = ranks};
	d_iov_t        iovs[]  = {{.iov_buf = "abcdef", .iov_buf_len = 128, .iov_len = 6}, {0}};
	struct message in      = {.number       = 0x12345678,
				  .string       = "hello",
				  .const_string = "constant",
				  .iov          = iovs[0],
				  .ranks        = &list,
				  .sgl          = {.sg_nr = 2, .sg_nr_out = 1, .sg_iovs = iovs}};
	size_t         size, used;
	assert(crt_native_cart_run(message, &in, CRT_PROC_ENCODE, frame, sizeof(frame), NULL, NULL,
				   &size) == 0);
	for (size_t n = 0; n <= size; n++) {
		struct message out = {0};
		int rc = crt_native_cart_run(message, &out, CRT_PROC_DECODE, frame, n, NULL, NULL,
					     &used);
		assert(rc == (n == size ? 0 : -DER_PROTO));
		if (rc == 0) {
			assert(out.number == in.number && strcmp(out.string, in.string) == 0);
			assert(strcmp(out.const_string, in.const_string) == 0);
			assert(out.iov.iov_buf_len == 6 && out.iov.iov_len == 6);
			assert((uint8_t *)out.iov.iov_buf > frame &&
			       (uint8_t *)out.iov.iov_buf < frame + size);
			assert(out.ranks->rl_nr == 3 && out.ranks->rl_ranks[2] == ranks[2]);
			assert(out.sgl.sg_nr == 2 && out.sgl.sg_nr_out == 1 &&
			       out.sgl.sg_iovs[1].iov_len == 0);
		}
		assert(crt_native_cart_run(message, &out, CRT_PROC_FREE, NULL, 0, NULL, NULL,
					   &used) == 0);
		assert(out.string == NULL && out.const_string == NULL && out.iov.iov_buf == NULL &&
		       out.ranks == NULL && out.sgl.sg_iovs == NULL);
	}
	assert(crt_native_cart_run(message, &in, CRT_PROC_ENCODE, frame, 10, NULL, NULL, &used) ==
	       -DER_TRUNC);
	assert(in.ranks == &list && in.sgl.sg_iovs == iovs && in.string != NULL &&
	       in.const_string != NULL && in.iov.iov_buf == iovs[0].iov_buf);
}

static int
bulk_only(crt_proc_t proc, void *arg)
{
	crt_proc_op_t op;
	int           rc = crt_proc_get_op(proc, &op);
	return rc ? rc : crt_proc_crt_bulk_t(proc, op, arg);
}

/* Binding boundary test only: the real bulk descriptor wire format and lease
 * ownership are covered by test_bulk. Here a binding resolves a local token.
 */
static int
bulk_binding(crt_proc_t proc, crt_proc_op_t op, crt_bulk_t *handle, void *arg)
{
	unsigned               *calls  = arg;
	struct crt_native_proc *cursor = crt_native_cart_cursor(proc);
	assert(cursor != NULL);
	(*calls)++;
	if (op == CRT_PROC_FREE) {
		assert(cursor->op == CRT_NATIVE_FREE);
		*handle = NULL;
		return 0;
	}
	uint64_t token = op == CRT_PROC_ENCODE ? 99 : 0;
	if (crt_native_proc_u64(cursor, &token))
		return -DER_PROTO;
	assert(token == 99);
	if (op == CRT_PROC_DECODE)
		*handle = arg; /* stand-in for connection-owned decoded wrapper */
	return 0;
}

static void
bound_bulk(void)
{
	unsigned   calls  = 0;
	crt_bulk_t handle = &calls;
	uint8_t    buffer[64];
	size_t     size, used;
	assert(crt_native_cart_run(bulk_only, &handle, CRT_PROC_ENCODE, buffer, sizeof(buffer),
				   NULL, NULL, &used) == -DER_NOSYS);
	assert(crt_native_cart_run(bulk_only, &handle, CRT_PROC_ENCODE, buffer, sizeof(buffer),
				   bulk_binding, &calls, &size) == 0);
	handle = NULL;
	assert(crt_native_cart_run(bulk_only, &handle, CRT_PROC_DECODE, buffer, size, bulk_binding,
				   &calls, &used) == 0 &&
	       handle == &calls);
	assert(crt_native_cart_run(bulk_only, &handle, CRT_PROC_FREE, NULL, 0, bulk_binding, &calls,
				   &used) == 0 &&
	       handle == NULL && calls == 3);
}

struct nested {
	crt_proc_t outer;
	bool       inner;
};
static int
nest(crt_proc_t proc, void *arg)
{
	struct nested *n      = arg;
	uint32_t       number = 7;
	crt_proc_op_t  op;
	assert(crt_proc_get_op(proc, &op) == 0 && op == CRT_PROC_ENCODE);
	if (!n->inner) {
		uint8_t       buffer[32];
		size_t        used;
		struct nested child = {.outer = proc, .inner = true};
		assert(crt_native_cart_run(nest, &child, CRT_PROC_ENCODE, buffer, sizeof(buffer),
					   NULL, NULL, &used) == 0);
	} else {
		assert(crt_proc_uint32_t(n->outer, op, &number) == 0);
	}
	int hg_cookie;
	assert(crt_proc_uint32_t(&hg_cookie, op, &number) == 123);
	return crt_proc_uint32_t(proc, op, &number);
}
static void *
thread(void *arg)
{
	(void)arg;
	for (unsigned i = 0; i < 1024; i++) {
		uint8_t       buffer[32];
		size_t        used;
		struct nested n = {0};
		assert(crt_native_cart_run(nest, &n, CRT_PROC_ENCODE, buffer, sizeof(buffer), NULL,
					   NULL, &used) == 0);
		assert(used == 16);
	}
	return NULL;
}

int
main(void)
{
	roundtrip();
	bound_bulk();
	assert(atomic_load(&fallbacks) == 0);
	pthread_t a, b;
	assert(pthread_create(&a, NULL, thread, NULL) == 0);
	assert(pthread_create(&b, NULL, thread, NULL) == 0);
	assert(pthread_join(a, NULL) == 0 && pthread_join(b, NULL) == 0);
	assert(atomic_load(&fallbacks) == 4096);
	puts("CaRT proc adapter: real API types, borrowed views, cleanup, nesting, thread "
	     "isolation and Mercury routing passed");
	return 0;
}
