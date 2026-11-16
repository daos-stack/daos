/*
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#ifndef CRT_NATIVE_BULK_SESSION_H
#define CRT_NATIVE_BULK_SESSION_H
#include "bulk.h"

struct crt_native_bulk_session;
bool
crt_native_bulk_session_on_endpoint(const struct crt_native_bulk_session *,
				    const struct crt_native_ops *, const struct crt_native_ep *);
/* Hooks pin the enclosing CaRT wrapper as well as its native registrations.
 * They run synchronously on the session owner; do not reenter the session.
 */
struct crt_native_bulk_owner {
	void *arg;
	void (*hold)(void *);
	void (*drop)(void *);
};
typedef int (*crt_native_ack_send_t)(void *, uint64_t lease_id);
/* Both peers use the negotiated capacity. The endpoint is the dedicated bulk
 * endpoint; ack_send queues RELEASE on the control channel. All session calls
 * and local bulk progress are serialized by their owner, including callbacks.
 */
int
crt_native_bulk_session_create(const struct crt_native_ops *, struct crt_native_ep *, unsigned,
			       crt_native_ack_send_t, void *, struct crt_native_bulk_session **);
int
crt_native_bulk_export_begin(struct crt_native_bulk_session *, struct crt_native_bulk *,
			     const struct crt_native_bulk_owner *, uint64_t *id);
int
crt_native_bulk_export_pack(struct crt_native_bulk_session *, uint64_t id, void *, size_t,
			    size_t *used);
/* DRAFT -> PUBLISHED before a message becomes visible to the peer. Only drafts
 * may be aborted without remote acknowledgment. An encoder must track its draft
 * IDs and abort them if serialization/queueing fails before publication.
 */
int
crt_native_bulk_export_publish(struct crt_native_bulk_session *, uint64_t id);
int
crt_native_bulk_export_abort(struct crt_native_bulk_session *, uint64_t id);
/* Callback-compatible with crt_native_release_t. Stale/duplicate ACKs are
 * harmless; future IDs and ACKs for unpublished drafts are protocol errors.
 */
int
crt_native_bulk_session_ack(void *, uint64_t id);
int
crt_native_bulk_import(struct crt_native_bulk_session *, const struct crt_native_bulk_remote *);
/* get pins the imported descriptor for a wrapper or an accepted transfer.
 * import itself owns one reference. Last put queues acknowledgment; never put
 * a transfer's reference until all accepted RDMA completions have drained.
 */
int
crt_native_bulk_import_get(struct crt_native_bulk_session *, uint64_t id,
			   const struct crt_native_bulk_remote **);
int
crt_native_bulk_import_put(struct crt_native_bulk_session *, uint64_t id);
/* At most 32 ACKs per call. EAGAIN retains the head for later progress. */
int
crt_native_bulk_session_progress(struct crt_native_bulk_session *);
/* A timeout may call shutdown, but cannot release any published exports.
 * Quiesce succeeds only after the transport proves remote DMA is impossible;
 * drain bulk completions separately and retry EBUSY/teardown errors.
 */
int
crt_native_bulk_session_shutdown(struct crt_native_bulk_session *);
int
crt_native_bulk_session_quiesce(struct crt_native_bulk_session *);
int
crt_native_bulk_session_destroy(struct crt_native_bulk_session *);
#endif
