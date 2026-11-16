/*
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#include "wire.h"
#include <errno.h>
#include <string.h>

static void
put32(uint8_t *p, uint32_t value)
{
	for (unsigned i = 0; i < 4; i++)
		p[i] = (uint8_t)(value >> (24 - i * 8));
}

static uint32_t
get32(const uint8_t *p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static int
valid(const struct crt_native_header *h)
{
	return h != NULL && h->request_id != 0 &&
	       (h->type == CRT_NATIVE_REQUEST || h->type == CRT_NATIVE_REPLY ||
		h->type == CRT_NATIVE_RELEASE) &&
	       (h->type != CRT_NATIVE_REQUEST || h->status == 0) &&
	       (h->type != CRT_NATIVE_RELEASE ||
		(h->status == 0 && h->opcode == 0 && h->payload_size == 0));
}

int
crt_native_encode(const struct crt_native_header *h, void *buffer, size_t capacity)
{
	uint8_t *p = buffer;

	if (!valid(h) || buffer == NULL || capacity < CRT_NATIVE_HEADER_SIZE)
		return EINVAL;
	memset(p, 0, CRT_NATIVE_HEADER_SIZE);
	memcpy(p, "CRTN", 4);
	p[4] = CRT_NATIVE_WIRE_VERSION;
	p[5] = (uint8_t)h->type;
	put32(p + 8, (uint32_t)(h->request_id >> 32));
	put32(p + 12, (uint32_t)h->request_id);
	put32(p + 16, h->opcode);
	put32(p + 20, h->payload_size);
	put32(p + 24, h->status);
	return 0;
}

int
crt_native_decode(const void *buffer, size_t size, struct crt_native_header *h)
{
	const uint8_t           *p = buffer;
	struct crt_native_header decoded;

	if (p == NULL || h == NULL || size < CRT_NATIVE_HEADER_SIZE)
		return EINVAL;
	if (memcmp(p, "CRTN", 4) != 0 || p[4] != CRT_NATIVE_WIRE_VERSION || p[6] != 0 ||
	    p[7] != 0 || get32(p + 28) != 0)
		return EPROTO;
	decoded = (struct crt_native_header){
	    .type         = p[5],
	    .request_id   = ((uint64_t)get32(p + 8) << 32) | get32(p + 12),
	    .opcode       = get32(p + 16),
	    .payload_size = get32(p + 20),
	    .status       = get32(p + 24),
	};
	if (!valid(&decoded) || decoded.payload_size != size - CRT_NATIVE_HEADER_SIZE)
		return EPROTO;
	*h = decoded;
	return 0;
}
