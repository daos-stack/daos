/*
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#ifndef CRT_NATIVE_CART_BULK_H
#define CRT_NATIVE_CART_BULK_H
#include "cart_proc.h"
#include "bulk_session.h"

struct crt_native_cart_bulk;
struct crt_native_cart_remote {
	struct crt_native_cart_remote       *next;
	struct crt_native_cart_bulk         *binding;
	struct crt_native_bulk_session      *session;
	const struct crt_native_bulk_remote *descriptor;
	crt_bulk_t                           handle;
};
/* A bounded connection-local registration cache. The session and endpoint
 * outlive this binding and every decoded handle. Holds a reference to cached
 * CaRT handles so registration never outlives application buffers' ownership.
 */
int
crt_native_cart_bulk_create(const struct crt_native_ops *, struct crt_native_ep *,
			    struct crt_native_bulk_session *, unsigned capacity,
			    struct crt_native_cart_bulk **);
int
crt_native_cart_bulk_destroy(struct crt_native_cart_bulk *);
int
crt_native_cart_bulk_progress(struct crt_native_cart_bulk *);
int
crt_native_cart_bulk_defer_callbacks(struct crt_native_cart_bulk *);
unsigned
crt_native_cart_bulk_dispatch(struct crt_native_cart_bulk *);
int
crt_native_cart_bulk_transfer(struct crt_native_cart_bulk *, struct crt_bulk_desc *,
			      crt_bulk_cb_t verify, crt_bulk_cb_t complete, void *,
			      crt_bulk_opid_t *);
/* Registry lookup never dereferences an unrecognized Mercury operation ID. */
bool
crt_native_cart_bulk_try_abort(crt_context_t, crt_bulk_opid_t, int *result);
/* Initialize on owner before publishing a decoded wrapper. Final release can
 * occur on an application worker; only owner progress mutates the session.
 */
void
crt_native_cart_remote_acquire(struct crt_native_cart_remote *);
void
crt_native_cart_remote_release(struct crt_native_cart_remote *);
/* Per-message stack transaction. Encode all fields with proc(), then commit
 * before publishing the frame, or abort if the encoder/queue fails first.
 * Do not commit while another thread may shut down the owning session.
 */
struct crt_native_cart_bulk_txn {
	struct crt_native_cart_bulk *binding;
	unsigned                     count;
	uint64_t                     exports[CRT_NATIVE_BULK_MAX_SEGMENTS];
};
int
crt_native_cart_bulk_proc(crt_proc_t, crt_proc_op_t, crt_bulk_t *, void *txn);
int
crt_native_cart_bulk_commit(struct crt_native_cart_bulk_txn *);
int
crt_native_cart_bulk_abort(struct crt_native_cart_bulk_txn *);
/* Registration lookup for local transfer buffers; borrowed while binding is
 * alive. The native transfer acquires its own local lease before posting.
 */
int
crt_native_cart_bulk_local(struct crt_native_cart_bulk *, crt_bulk_t, struct crt_native_bulk **);

/* Internal helpers implemented alongside the concrete CaRT bulk wrapper. */
int
crt_native_cart_wrap_remote(struct crt_native_cart_bulk *, struct crt_native_bulk_session *,
			    uint64_t, const struct crt_native_bulk_remote *, crt_bulk_t *);
int
crt_native_cart_remote_info(crt_bulk_t, struct crt_native_cart_bulk **,
			    struct crt_native_bulk_session **,
			    const struct crt_native_bulk_remote **);
int
crt_native_cart_bulk_permission(crt_bulk_t, crt_bulk_perm_t *);
void
crt_native_cart_destroy_remote(struct crt_native_cart_remote *);
bool
crt_native_cart_bulk_expired(struct crt_bulk_desc *);
#endif
