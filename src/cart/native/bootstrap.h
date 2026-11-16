/*
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#ifndef CRT_NATIVE_BOOTSTRAP_H
#define CRT_NATIVE_BOOTSTRAP_H
#include "transport.h"

#define CRT_NATIVE_HELLO_SIZE 48

struct crt_native_limits {
	uint32_t frame_size;
	uint32_t receives;
};

int
crt_native_hello_encode(const struct crt_native_address *, const struct crt_native_limits *,
			uint8_t[CRT_NATIVE_HELLO_SIZE]);
int
crt_native_hello_decode(const uint8_t[CRT_NATIVE_HELLO_SIZE], struct crt_native_address *,
			struct crt_native_limits *);

/* Both peers call on an already connected stream socket. The bootstrap owner
 * authenticates the peer before invoking this function. No memory keys are
 * exchanged here. The fd remains owned by the caller. timeout_ms is a single
 * deadline for the entire exchange, not a per-read timeout. No CaRT/Mercury,
 * libibverbs, DOCA, or CUDA types enter the wire format.
 */
int
crt_native_bootstrap(const struct crt_native_ops *, struct crt_native_ep *, int fd,
		     const struct crt_native_limits *, unsigned timeout_ms,
		     struct crt_native_limits *negotiated);
#endif
