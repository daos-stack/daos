/*
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#ifndef CRT_NATIVE_PROC_H
#define CRT_NATIVE_PROC_H
#include "bulk.h"

/* Allocation-free, bounded payload codec, distinct from Mercury/XDR. Integers
 * are big endian, with no alignment padding or native C structs on the wire.
 * The eight-byte versioned prefix must be present even for an empty payload.
 * Positive errno results are sticky until init. Do not modify cursor fields.
 */
#define CRT_NATIVE_PROC_HEADER 8
enum crt_native_proc_op { CRT_NATIVE_ENCODE, CRT_NATIVE_DECODE, CRT_NATIVE_FREE };
struct crt_native_proc {
	uint8_t                *buffer;
	size_t                  capacity, offset;
	enum crt_native_proc_op op;
	int                     error;
};

/* DECODE never modifies buffer. The non-const argument also permits ENCODE.
 * FREE uses no buffer; it resets borrowed views and never frees their memory.
 */
int
crt_native_proc_init(struct crt_native_proc *, enum crt_native_proc_op, void *, size_t);
/* ENCODE returns used length; DECODE also requires exact consumption. */
int
crt_native_proc_finish(struct crt_native_proc *, size_t *used);
int
crt_native_proc_u8(struct crt_native_proc *, uint8_t *);
int
crt_native_proc_u16(struct crt_native_proc *, uint16_t *);
int
crt_native_proc_u32(struct crt_native_proc *, uint32_t *);
int
crt_native_proc_u64(struct crt_native_proc *, uint64_t *);
/* Signed values use canonical two's-complement wire representation. */
int
crt_native_proc_i8(struct crt_native_proc *, int8_t *);
int
crt_native_proc_i16(struct crt_native_proc *, int16_t *);
int
crt_native_proc_i32(struct crt_native_proc *, int32_t *);
int
crt_native_proc_i64(struct crt_native_proc *, int64_t *);
int
crt_native_proc_bool(struct crt_native_proc *, bool *);
/* Opaque fixed-size bytes, e.g. UUID. Copies only the supplied field. */
int
crt_native_proc_raw(struct crt_native_proc *, void *, size_t);
/* Bounds-checked cursor reservation; ENCODE callers can produce a field
 * directly in the final registered frame. DECODE returns a borrowed view.
 * On FREE returns NULL without advancing. No unaligned typed dereferences.
 */
int
crt_native_proc_span(struct crt_native_proc *, size_t, void **);
/* A u64 length followed by bytes. DECODE aliases the input and performs no
 * allocation/copy. max is an application limit, applied before cursor advance.
 * On ENCODE, copies application bytes once into the final frame. For entirely
 * copy-free production use u64()+span() and fill the reserved span directly.
 */
int
crt_native_proc_bytes(struct crt_native_proc *, void **data, size_t *length, size_t max);
/* Length includes a terminating NUL; zero means NULL. Embedded NUL rejected.
 * DECODE returns borrowed storage; FREE only clears the pointer.
 */
int
crt_native_proc_string(struct crt_native_proc *, char **, size_t max);
/* Serialize a pre-acquired export lease, never application payload bytes.
 * ENCODE requires lease; DECODE requires remote. A zero descriptor length
 * represents NULL. This codec does not acquire/release leases: the enclosing
 * RPC must retain them through peer acknowledgment or remote quiescence.
 */
int
crt_native_proc_bulk(struct crt_native_proc *, const struct crt_native_bulk_lease *,
		     struct crt_native_bulk_remote *);
#endif
