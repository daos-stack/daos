/*
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#ifndef CRT_NATIVE_BULK_H
#define CRT_NATIVE_BULK_H
#include "transport.h"

#define CRT_NATIVE_BULK_MAX_SEGMENTS 64
#define CRT_NATIVE_BULK_HEADER       16
#define CRT_NATIVE_BULK_SEGMENT      24
struct crt_native_bulk;
bool
crt_native_bulk_on_endpoint(const struct crt_native_bulk *, const struct crt_native_ops *,
			    const struct crt_native_ep *);
struct crt_native_iov {
	void  *base;
	size_t size;
};
struct crt_native_bulk_remote {
	uint64_t                        lease_id;
	uint64_t                        length;
	unsigned                        access, count;
	struct crt_native_remote_memory segments[CRT_NATIVE_BULK_MAX_SEGMENTS];
};
struct crt_native_bulk_lease {
	struct crt_native_bulk *owner;
	uint64_t                id;
};

#define CRT_NATIVE_BULK_MAX_CHUNKS 128
struct crt_native_bulk_chunk {
	struct crt_native_mr           *local;
	struct crt_native_remote_memory remote;
	size_t                          local_offset, remote_offset, size;
	unsigned                        state;
};
/* Caller-owned, zero-initialize before first use; do not copy while active.
 * Use a dedicated bulk endpoint/progress owner, not a channel-owned RPC QP.
 */
struct crt_native_bulk_transfer {
	const struct crt_native_ops *ops;
	struct crt_native_ep        *ep;
	struct crt_native_bulk_lease local_lease;
	enum crt_native_opcode       opcode;
	unsigned                     count, next, pending;
	int                          error;
	struct crt_native_bulk_chunk chunks[CRT_NATIVE_BULK_MAX_CHUNKS];
};

int
crt_native_bulk_transfer_init(struct crt_native_bulk_transfer *, const struct crt_native_ops *,
			      struct crt_native_ep *, struct crt_native_bulk *,
			      const struct crt_native_bulk_remote *, enum crt_native_opcode,
			      uint64_t local_offset, uint64_t remote_offset, uint64_t size);
/* Submit up to 32 chunks. EAGAIN is retryable. Other submission errors stop
 * unsubmitted chunks, but accepted chunks still own their buffers. Always use
 * transfer_status() to decide when a transfer is terminal.
 */
int
crt_native_bulk_transfer_post(struct crt_native_bulk_transfer *);
int
crt_native_bulk_transfer_complete(struct crt_native_bulk_transfer *,
				  const struct crt_native_completion *);
int
crt_native_bulk_transfer_cancel(struct crt_native_bulk_transfer *);
int
crt_native_bulk_transfer_status(const struct crt_native_bulk_transfer *);
int
crt_native_bulk_transfer_fini(struct crt_native_bulk_transfer *);

/* Registration is connection-bound: ep must be the bulk QP's endpoint. It is
 * not interchangeable with an independently allocated control-QP protection
 * domain. Both peers exchange these descriptors on the control channel only
 * after a trusted bootstrap has connected the bulk endpoints. Authentication
 * is the bootstrap owner's responsibility, not a property of this descriptor.
 * Partial rollback errors return a non-NULL handle for retrying destroy().
 */
int
crt_native_bulk_create(const struct crt_native_ops *, struct crt_native_ep *,
		       const struct crt_native_iov *, unsigned count, unsigned access,
		       struct crt_native_bulk **);
int
crt_native_bulk_destroy(struct crt_native_bulk *);
/* A zero-initialized caller-owned lease pins all exported registrations.
 * id is the enclosing RPC's nonzero, generation-tagged lease identifier.
 * Pack is allocation-free. Every successful acquire requires one release.
 */
int
crt_native_bulk_acquire(struct crt_native_bulk *, uint64_t id, struct crt_native_bulk_lease *);
int
crt_native_bulk_pack(const struct crt_native_bulk_lease *, void *, size_t capacity, size_t *used);
int
crt_native_bulk_unpack(const void *, size_t, struct crt_native_bulk_remote *);
/* Call ONLY after the peer has relinquished the key (e.g. its completed bulk
 * operation was acknowledged), or connection teardown established remote
 * quiescence. A local RPC timeout alone is not sufficient to release a lease.
 */
int
crt_native_bulk_release(struct crt_native_bulk_lease *);
/* Resolve a bounded SGL slice for a separate bulk-transfer progress owner.
 * Caller must keep the local handle alive through every accepted completion.
 * offset and remaining are logical SGL offsets, never raw pointer arithmetic.
 */
int
crt_native_bulk_local_slice(struct crt_native_bulk *, uint64_t offset, uint64_t remaining,
			    struct crt_native_mr **, size_t *mr_offset, size_t *size);
int
crt_native_bulk_remote_slice(const struct crt_native_bulk_remote *, unsigned required_access,
			     uint64_t offset, uint64_t remaining, struct crt_native_remote_memory *,
			     size_t *mr_offset, size_t *size);
#endif
