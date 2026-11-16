/*
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#include "proc.h"
#include <errno.h>
#include <string.h>

static int
fail(struct crt_native_proc *p, int error)
{
	if (p == NULL)
		return EINVAL;
	if (p->error == 0)
		p->error = error;
	return p->error;
}

int
crt_native_proc_span(struct crt_native_proc *p, size_t size, void **out)
{
	if (p == NULL || out == NULL)
		return fail(p, EINVAL);
	*out = NULL;
	if (p->error)
		return p->error;
	if (p->op == CRT_NATIVE_FREE)
		return 0;
	if (size > p->capacity - p->offset)
		return fail(p, p->op == CRT_NATIVE_ENCODE ? EMSGSIZE : EPROTO);
	*out = p->buffer + p->offset;
	p->offset += size;
	return 0;
}

int
crt_native_proc_init(struct crt_native_proc *p, enum crt_native_proc_op op, void *buffer,
		     size_t capacity)
{
	static const uint8_t header[CRT_NATIVE_PROC_HEADER] = {'C', 'R', 'T', 'P', 1, 0, 0, 0};
	void                *view;
	int                  rc;

	if (p == NULL)
		return EINVAL;
	*p = (struct crt_native_proc){.buffer = buffer, .capacity = capacity, .op = op};
	if (op != CRT_NATIVE_ENCODE && op != CRT_NATIVE_DECODE && op != CRT_NATIVE_FREE)
		return fail(p, EINVAL);
	if (op == CRT_NATIVE_FREE)
		return 0;
	if (buffer == NULL)
		return fail(p, EINVAL);
	rc = crt_native_proc_span(p, sizeof(header), &view);
	if (rc != 0)
		return rc;
	if (op == CRT_NATIVE_ENCODE)
		memcpy(view, header, sizeof(header));
	else if (memcmp(view, header, sizeof(header)) != 0)
		return fail(p, EPROTO);
	return 0;
}

int
crt_native_proc_finish(struct crt_native_proc *p, size_t *used)
{
	if (p == NULL || used == NULL)
		return fail(p, EINVAL);
	if (p->error)
		return p->error;
	if (p->op == CRT_NATIVE_DECODE && p->offset != p->capacity)
		return fail(p, EPROTO);
	*used = p->offset;
	return 0;
}

static int
integer(struct crt_native_proc *p, uint64_t *value, unsigned size)
{
	void    *view;
	uint8_t *bytes;
	uint64_t v  = 0;
	int      rc = crt_native_proc_span(p, size, &view);
	if (rc != 0 || p->op == CRT_NATIVE_FREE)
		return rc;
	bytes = view;
	if (p->op == CRT_NATIVE_ENCODE) {
		for (unsigned i = 0; i < size; i++)
			bytes[size - 1 - i] = (uint8_t)(*value >> (i * 8));
	} else {
		for (unsigned i = 0; i < size; i++)
			v = v << 8 | bytes[i];
		*value = v;
	}
	return 0;
}

#define INTEGER(bits)                                                                              \
	int crt_native_proc_u##bits(struct crt_native_proc *p, uint##bits##_t *value)              \
	{                                                                                          \
		uint64_t v;                                                                        \
		int      rc;                                                                       \
		if (p == NULL || value == NULL)                                                    \
			return fail(p, EINVAL);                                                    \
		v  = p->op == CRT_NATIVE_ENCODE ? *value : 0;                                      \
		rc = integer(p, &v, sizeof(*value));                                               \
		if (rc == 0 && p->op == CRT_NATIVE_DECODE)                                         \
			*value = (uint##bits##_t)v;                                                \
		return rc;                                                                         \
	}
INTEGER(8)
INTEGER(16)
INTEGER(32)
INTEGER(64)

#define SIGNED_INTEGER(bits)                                                                       \
	int crt_native_proc_i##bits(struct crt_native_proc *p, int##bits##_t *value)               \
	{                                                                                          \
		uint##bits##_t v;                                                                  \
		int            rc;                                                                 \
		if (p == NULL || value == NULL)                                                    \
			return fail(p, EINVAL);                                                    \
		v  = p->op == CRT_NATIVE_ENCODE ? (uint##bits##_t) * value : 0;                    \
		rc = crt_native_proc_u##bits(p, &v);                                               \
		if (rc == 0 && p->op == CRT_NATIVE_DECODE)                                         \
			*value =                                                                   \
			    v <= INT##bits##_MAX                                                   \
				? (int##bits##_t)v                                                 \
				: (int##bits##_t)(-1 - (int##bits##_t)(UINT##bits##_MAX - v));     \
		return rc;                                                                         \
	}
SIGNED_INTEGER(8)
SIGNED_INTEGER(16)
SIGNED_INTEGER(32)
SIGNED_INTEGER(64)

int
crt_native_proc_bool(struct crt_native_proc *p, bool *value)
{
	uint8_t v;
	int     rc;
	if (p == NULL || value == NULL)
		return fail(p, EINVAL);
	v  = p->op == CRT_NATIVE_ENCODE && *value ? 1 : 0;
	rc = crt_native_proc_u8(p, &v);
	if (rc != 0 || p->op != CRT_NATIVE_DECODE)
		return rc;
	if (v > 1)
		return fail(p, EPROTO);
	*value = v != 0;
	return 0;
}

int
crt_native_proc_raw(struct crt_native_proc *p, void *data, size_t size)
{
	void *view;
	int   rc;
	if (size != 0 && data == NULL)
		return fail(p, EINVAL);
	rc = crt_native_proc_span(p, size, &view);
	if (rc != 0 || p->op == CRT_NATIVE_FREE || size == 0)
		return rc;
	if (p->op == CRT_NATIVE_ENCODE)
		memmove(view, data, size);
	else
		memmove(data, view, size);
	return 0;
}

int
crt_native_proc_bytes(struct crt_native_proc *p, void **data, size_t *length, size_t max)
{
	uint64_t size;
	void    *view;
	int      rc;
	if (p == NULL || data == NULL || length == NULL)
		return fail(p, EINVAL);
	if (p->error)
		return p->error;
	if (p->op == CRT_NATIVE_FREE) {
		*data   = NULL;
		*length = 0;
		return 0;
	}
	size = p->op == CRT_NATIVE_ENCODE ? *length : 0;
	if (p->op == CRT_NATIVE_ENCODE && (size > max || (size && *data == NULL)))
		return fail(p, EINVAL);
	rc = crt_native_proc_u64(p, &size);
	if (rc != 0)
		return rc;
	if (size > max || size > SIZE_MAX)
		return fail(p, EPROTO);
	rc = crt_native_proc_span(p, (size_t)size, &view);
	if (rc != 0)
		return rc;
	if (p->op == CRT_NATIVE_ENCODE) {
		if (size)
			memmove(view, *data, (size_t)size);
	} else {
		*data   = size ? view : NULL;
		*length = (size_t)size;
	}
	return 0;
}

int
crt_native_proc_string(struct crt_native_proc *p, char **string, size_t max)
{
	void  *data;
	size_t length = 0;
	int    rc;
	if (p == NULL || string == NULL)
		return fail(p, EINVAL);
	if (p->error)
		return p->error;
	/* Bounded scan: never strlen() an untrusted/application-unbounded field. */
	data = p->op == CRT_NATIVE_ENCODE ? *string : NULL;
	if (p->op == CRT_NATIVE_ENCODE && data != NULL) {
		const char *end = memchr(data, 0, max);
		if (end == NULL)
			return fail(p, EINVAL);
		length = (size_t)(end - *string) + 1;
	}
	rc = crt_native_proc_bytes(p, &data, &length, max);
	if (rc != 0)
		return rc;
	if (p->op == CRT_NATIVE_DECODE && length &&
	    (((char *)data)[length - 1] != 0 || memchr(data, 0, length - 1) != NULL))
		return fail(p, EPROTO);
	if (p->op != CRT_NATIVE_ENCODE)
		*string = data;
	return 0;
}

int
crt_native_proc_bulk(struct crt_native_proc *p, const struct crt_native_bulk_lease *lease,
		     struct crt_native_bulk_remote *remote)
{
	uint32_t size = 0;
	size_t   used, start;
	void    *view;
	int      rc;
	if (p == NULL)
		return EINVAL;
	if (p->error)
		return p->error;
	if (p->op == CRT_NATIVE_FREE) {
		if (remote != NULL)
			memset(remote, 0, sizeof(*remote));
		return 0;
	}
	if (p->op == CRT_NATIVE_DECODE && remote == NULL)
		return fail(p, EINVAL);
	start = p->offset;
	rc    = crt_native_proc_u32(p, &size);
	if (rc != 0)
		return rc;
	if (p->op == CRT_NATIVE_ENCODE && lease != NULL) {
		rc = crt_native_bulk_pack(lease, p->buffer + p->offset, p->capacity - p->offset,
					  &used);
		if (rc != 0)
			return fail(p, rc);
		size      = (uint32_t)used; /* pack is bounded to 64 segments */
		p->offset = start;
		crt_native_proc_u32(p, &size);
	}
	rc = crt_native_proc_span(p, size, &view);
	if (rc != 0)
		return rc;
	if (p->op == CRT_NATIVE_DECODE) {
		if (size == 0)
			memset(remote, 0, sizeof(*remote));
		else if ((rc = crt_native_bulk_unpack(view, size, remote)) != 0)
			return fail(p, rc);
	}
	return 0;
}
