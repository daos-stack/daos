/*
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#ifndef CRT_NATIVE_WIRE_H
#define CRT_NATIVE_WIRE_H
#include <stddef.h>
#include <stdint.h>

/* Experimental protocol, distinct from Mercury's wire protocol. No native C
 * structures or pointers are transmitted. The RPC payload codec is a separate
 * concern; this envelope is usable by both CPU and future GPU datapaths.
 */
#define CRT_NATIVE_HEADER_SIZE  32
#define CRT_NATIVE_WIRE_VERSION 1
enum crt_native_message_type {
	CRT_NATIVE_REQUEST = 1,
	CRT_NATIVE_REPLY   = 2,
	CRT_NATIVE_RELEASE = 3
};

struct crt_native_header {
	enum crt_native_message_type type;
	uint64_t                     request_id;
	uint32_t                     opcode;
	uint32_t                     payload_size;
	/* Unsigned protocol-defined status; never a platform-native errno. */
	uint32_t                     status;
};

int
crt_native_encode(const struct crt_native_header *, void *, size_t);
/* Decode an entire frame; reject trailing bytes, unknown versions and flags. */
int
crt_native_decode(const void *, size_t, struct crt_native_header *);
#endif
