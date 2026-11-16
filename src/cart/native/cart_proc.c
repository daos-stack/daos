/*
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#include "cart_proc.h"
#include <errno.h>
#include <stdlib.h>

struct scope {
	struct crt_native_proc    cursor;
	struct scope             *previous;
	crt_native_cart_bulk_cb_t bulk;
	void                     *bulk_arg;
	crt_proc_op_t             op;
};
static _Thread_local struct scope *active;

static struct scope *
lookup(crt_proc_t proc)
{
	for (struct scope *s = active; s != NULL; s = s->previous)
		if (proc == &s->cursor)
			return s;
	return NULL;
}

struct crt_native_proc *
crt_native_cart_cursor(crt_proc_t proc)
{
	struct scope *s = lookup(proc);
	return s == NULL ? NULL : &s->cursor;
}

static int
result(int rc)
{
	switch (rc) {
	case 0:
		return 0;
	case ENOMEM:
		return -DER_NOMEM;
	case EMSGSIZE:
		return -DER_TRUNC;
	case EPROTO:
		return -DER_PROTO;
	case EOVERFLOW:
		return -DER_OVERFLOW;
	default:
		return -DER_INVAL;
	}
}

static int
check(struct scope *s, crt_proc_op_t op)
{
	/* Generated CaRT callbacks unwind already decoded fields with FREE on
	 * the same proc after an error. Cleanup must ignore the sticky error.
	 */
	if (op == CRT_PROC_FREE)
		return 0;
	if (s->cursor.error == 0 && s->op != op)
		s->cursor.error = EINVAL;
	return result(s->cursor.error);
}

int
crt_native_cart_run(crt_proc_cb_t cb, void *data, crt_proc_op_t op, void *buffer, size_t size,
		    crt_native_cart_bulk_cb_t bulk, void *arg, size_t *used)
{
	struct scope            s = {.previous = active, .bulk = bulk, .bulk_arg = arg, .op = op};
	enum crt_native_proc_op native_op;
	int                     rc;
	if (cb == NULL || used == NULL)
		return -DER_INVAL;
	switch (op) {
	case CRT_PROC_ENCODE:
		native_op = CRT_NATIVE_ENCODE;
		break;
	case CRT_PROC_DECODE:
		native_op = CRT_NATIVE_DECODE;
		break;
	case CRT_PROC_FREE:
		native_op = CRT_NATIVE_FREE;
		break;
	default:
		return -DER_INVAL;
	}
	rc = crt_native_proc_init(&s.cursor, native_op, buffer, size);
	if (rc != 0)
		return result(rc);
	active = &s;
	rc     = cb(&s.cursor, data);
	if (rc == 0)
		rc = result(crt_native_proc_finish(&s.cursor, used));
	active = s.previous;
	return rc;
}

int
crt_proc_get_op(crt_proc_t proc, crt_proc_op_t *op)
{
	struct scope *s = lookup(proc);
	if (s == NULL)
		return crt_hg_proc_get_op(proc, op);
	if (op == NULL)
		return -DER_INVAL;
	*op = s->op;
	return 0;
}

#define SCALAR(type, method)                                                                       \
	int crt_proc_##type(crt_proc_t proc, crt_proc_op_t op, type *value)                        \
	{                                                                                          \
		struct scope *s = lookup(proc);                                                    \
		if (s == NULL)                                                                     \
			return crt_hg_proc_##type(proc, op, value);                                \
		if (op == CRT_PROC_FREE)                                                           \
			return 0;                                                                  \
		int rc = check(s, op);                                                             \
		return rc ? rc : result(crt_native_proc_##method(&s->cursor, value));              \
	}
SCALAR(int8_t, i8)
SCALAR(uint8_t, u8)
SCALAR(int16_t, i16)
SCALAR(uint16_t, u16)
SCALAR(int32_t, i32)
SCALAR(uint32_t, u32)
SCALAR(int64_t, i64)
SCALAR(uint64_t, u64)
SCALAR(bool, bool)

int
crt_proc_memcpy(crt_proc_t proc, crt_proc_op_t op, void *data, size_t size)
{
	struct scope *s = lookup(proc);
	if (s == NULL)
		return crt_hg_proc_memcpy(proc, op, data, size);
	if (op == CRT_PROC_FREE)
		return 0;
	int rc = check(s, op);
	return rc ? rc : result(crt_native_proc_raw(&s->cursor, data, size));
}

int
crt_proc_d_string_t(crt_proc_t proc, crt_proc_op_t op, d_string_t *value)
{
	struct scope *s = lookup(proc);
	if (s == NULL)
		return crt_hg_proc_d_string_t(proc, op, value);
	if (op == CRT_PROC_FREE) {
		if (s->op == CRT_PROC_ENCODE)
			return 0; /* encode-error unwind must not clear application input */
		if (value == NULL)
			return -DER_INVAL;
		*value = NULL;
		return 0;
	}
	int rc = check(s, op);
	/* Eager frames are bounded; never scan beyond a frame-sized string. */
	return rc ? rc : result(crt_native_proc_string(&s->cursor, value, 1048576));
}

int
crt_proc_d_const_string_t(crt_proc_t proc, crt_proc_op_t op, d_const_string_t *value)
{
	struct scope *s = lookup(proc);
	if (s == NULL)
		return crt_hg_proc_d_const_string_t(proc, op, value);
	if (value == NULL)
		return -DER_INVAL;
	char *mutable = op == CRT_PROC_ENCODE ? (char *)*value : NULL;
	int rc        = crt_proc_d_string_t(proc, op, &mutable);
	if (rc == 0 && op != CRT_PROC_ENCODE && !(op == CRT_PROC_FREE && s->op == CRT_PROC_ENCODE))
		*value = mutable;
	return rc;
}

int
crt_proc_d_iov_t(crt_proc_t proc, crt_proc_op_t op, d_iov_t *iov)
{
	struct scope *s = lookup(proc);
	if (s == NULL)
		return crt_hg_proc_d_iov_t(proc, op, iov);
	int rc = check(s, op);
	if (rc || iov == NULL)
		return rc ? rc : -DER_INVAL;
	if (op == CRT_PROC_FREE) {
		if (s->op == CRT_PROC_ENCODE)
			return 0;
		*iov = (d_iov_t){0};
		return 0;
	}
	/* Only transmitted length is writable in a borrowed view. Advertising
	 * the sender's larger capacity would permit writes past the RX frame.
	 */
	void  *data   = op == CRT_PROC_ENCODE ? iov->iov_buf : NULL;
	size_t length = op == CRT_PROC_ENCODE ? iov->iov_len : 0;
	if (op == CRT_PROC_ENCODE && length > iov->iov_buf_len)
		return -DER_INVAL;
	rc = result(crt_native_proc_bytes(&s->cursor, &data, &length, 1048576));
	if (rc == 0 && op == CRT_PROC_DECODE)
		*iov = (d_iov_t){.iov_buf = data, .iov_len = length, .iov_buf_len = length};
	return rc;
}

int
crt_proc_d_rank_list_t(crt_proc_t proc, crt_proc_op_t op, d_rank_list_t **value)
{
	struct scope *s = lookup(proc);
	if (s == NULL)
		return crt_hg_proc_d_rank_list_t(proc, op, value);
	int rc = check(s, op);
	if (rc || value == NULL)
		return rc ? rc : -DER_INVAL;
	if (op == CRT_PROC_FREE) {
		if (s->op == CRT_PROC_ENCODE)
			return 0;
		if (*value != NULL) {
			free((*value)->rl_ranks);
			free(*value);
		}
		*value = NULL;
		return 0;
	}
	uint32_t count = op == CRT_PROC_ENCODE && *value != NULL ? (*value)->rl_nr : 0;
	rc             = crt_proc_uint32_t(proc, op, &count);
	if (rc != 0)
		return rc;
	if (count > (s->cursor.capacity - s->cursor.offset) / sizeof(uint32_t))
		return result(s->cursor.error = op == CRT_PROC_DECODE ? EPROTO : EMSGSIZE);
	if (op == CRT_PROC_DECODE) {
		*value = NULL;
		if (count == 0)
			return 0;
		*value = calloc(1, sizeof(**value));
		if (*value == NULL)
			return -DER_NOMEM;
		(*value)->rl_ranks = calloc(count, sizeof(d_rank_t));
		if ((*value)->rl_ranks == NULL) {
			free(*value);
			*value = NULL;
			return -DER_NOMEM;
		}
		(*value)->rl_nr = count;
	} else if (count && (*value)->rl_ranks == NULL) {
		return -DER_INVAL;
	}
	for (uint32_t i = 0; i < count; i++) {
		rc = crt_proc_uint32_t(proc, op, &(*value)->rl_ranks[i]);
		if (rc != 0)
			return rc;
	}
	return 0;
}

int
crt_proc_d_sg_list_t(crt_proc_t proc, crt_proc_op_t op, d_sg_list_t *sgl)
{
	struct scope *s = lookup(proc);
	if (s == NULL)
		return crt_hg_proc_d_sg_list_t(proc, op, sgl);
	int rc = check(s, op);
	if (rc || sgl == NULL)
		return rc ? rc : -DER_INVAL;
	if (op == CRT_PROC_FREE) {
		if (s->op == CRT_PROC_ENCODE)
			return 0;
		free(sgl->sg_iovs);
		*sgl = (d_sg_list_t){0};
		return 0;
	}
	uint32_t count = op == CRT_PROC_ENCODE ? sgl->sg_nr : 0;
	uint32_t out   = op == CRT_PROC_ENCODE ? sgl->sg_nr_out : 0;
	if ((rc = crt_proc_uint32_t(proc, op, &count)) != 0 ||
	    (rc = crt_proc_uint32_t(proc, op, &out)) != 0)
		return rc;
	if (out > count || count > (s->cursor.capacity - s->cursor.offset) / sizeof(uint64_t))
		return result(s->cursor.error = op == CRT_PROC_DECODE ? EPROTO : EMSGSIZE);
	d_iov_t *iovs = op == CRT_PROC_ENCODE ? sgl->sg_iovs : NULL;
	if (op == CRT_PROC_DECODE && count) {
		iovs = calloc(count, sizeof(*iovs));
		if (iovs == NULL)
			return -DER_NOMEM;
	}
	if (count && iovs == NULL)
		return -DER_INVAL;
	for (uint32_t i = 0; i < count; i++) {
		rc = crt_proc_d_iov_t(proc, op, &iovs[i]);
		if (rc != 0) {
			if (op == CRT_PROC_DECODE)
				free(iovs);
			return rc;
		}
	}
	if (op == CRT_PROC_DECODE)
		*sgl = (d_sg_list_t){.sg_iovs = iovs, .sg_nr = count, .sg_nr_out = out};
	return 0;
}

int
crt_proc_crt_bulk_t(crt_proc_t proc, crt_proc_op_t op, crt_bulk_t *bulk)
{
	struct scope *s = lookup(proc);
	if (s == NULL)
		return crt_hg_proc_crt_bulk_t(proc, op, bulk);
	int rc = check(s, op);
	if (rc || bulk == NULL)
		return rc ? rc : -DER_INVAL;
	if (op == CRT_PROC_FREE && s->op == CRT_PROC_ENCODE)
		return 0;
	if (s->bulk != NULL) {
		if (op != CRT_PROC_FREE)
			return s->bulk(proc, op, bulk, s->bulk_arg);
		struct scope cleanup = {.previous = active,
					.bulk     = s->bulk,
					.bulk_arg = s->bulk_arg,
					.op       = CRT_PROC_FREE};
		crt_native_proc_init(&cleanup.cursor, CRT_NATIVE_FREE, NULL, 0);
		active = &cleanup;
		rc     = s->bulk(&cleanup.cursor, op, bulk, s->bulk_arg);
		active = cleanup.previous;
		return rc;
	}
	/* Do not interpret Mercury bulk handles as native registrations. */
	if (op == CRT_PROC_ENCODE)
		return *bulk != NULL ? -DER_NOSYS
				     : result(crt_native_proc_bulk(&s->cursor, NULL, NULL));
	if (op == CRT_PROC_FREE)
		return *bulk == NULL ? 0 : -DER_NOSYS;
	struct crt_native_bulk_remote remote;
	rc = result(crt_native_proc_bulk(&s->cursor, NULL, &remote));
	if (rc == 0 && remote.lease_id != 0)
		return -DER_NOSYS;
	if (rc == 0)
		*bulk = NULL;
	return rc;
}
