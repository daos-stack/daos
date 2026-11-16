/*
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#ifndef CRT_NATIVE_CHANNEL_H
#define CRT_NATIVE_CHANNEL_H
#include "bootstrap.h"
#include "rpc.h"

struct crt_native_channel;
int
crt_native_channel_cancel(struct crt_native_channel *, uint64_t id, int error);
int
crt_native_channel_set_release_handler(struct crt_native_channel *, crt_native_release_t, void *);
int
crt_native_channel_queue_release(struct crt_native_channel *, uint64_t lease_id);
int
crt_native_channel_set_deadline(struct crt_native_channel *, uint64_t id, uint64_t deadline_ns);
uint64_t
crt_native_channel_next_deadline(const struct crt_native_channel *);
/* Caller-owned, initially zero; do not copy an active lease. Retain only on
 * the channel's progress thread during a receive callback. Release may run
 * on a worker thread, once; reposting remains the progress owner's job.
 */
struct crt_native_rx_lease {
	struct crt_native_channel *channel;
	unsigned                   index;
	uint64_t                   generation;
};
int
crt_native_channel_retain(struct crt_native_channel *, const void *view, size_t,
			  struct crt_native_rx_lease *);
int
crt_native_channel_release(struct crt_native_rx_lease *);

/* The endpoint is exclusively owned by this channel until destroy, but must
 * be closed by the caller afterwards. Create registers pools without posting;
 * start preposts receives and may be called before the bootstrap exchange.
 * The limits must be the negotiated limits (or conservative equal limits on
 * both peers), and capacity must be at least 2*limits.receives. The effective
 * outbound/inbound RPC depth is floor(limits.receives/2).
 * If rollback deregistration fails, create returns an error with a non-NULL
 * channel; retain it and retry destroy rather than freeing registered memory.
 */
int
crt_native_channel_create(const struct crt_native_ops *, struct crt_native_ep *,
			  const struct crt_native_limits *, crt_native_handler_t, void *,
			  struct crt_native_channel **);
int
crt_native_channel_start(struct crt_native_channel *);
int
crt_native_channel_call(struct crt_native_channel *, uint32_t, const void *, size_t,
			crt_native_done_t, void *, uint64_t *);
int
crt_native_channel_call_encoded(struct crt_native_channel *, uint32_t, crt_native_encoder_t, void *,
				crt_native_done_t, void *, uint64_t *);
/* Bounded, nonblocking. Callbacks must not reenter progress or destroy.
 * Nonzero return initiates shutdown; continue progress to reap accepted work.
 */
int
crt_native_channel_progress(struct crt_native_channel *, unsigned *completed);
int
crt_native_channel_shutdown(struct crt_native_channel *, int error);
/* EBUSY while NIC work remains. Deregisters pools but does not close endpoint. */
int
crt_native_channel_destroy(struct crt_native_channel *);
#endif
