/*
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#ifndef CRT_NATIVE_TRANSPORT_H
#define CRT_NATIVE_TRANSPORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Experimental private interface; no public CaRT ABI. All entry points on an
 * endpoint and its memory registrations must be serialized by their owner.
 * Errors are positive errno values. A successful post accepts an operation;
 * its buffer and completion object remain owned until returned by progress().
 * Failed posts do not generate completions. Neither QP errors nor close_begin()
 * release buffers: their flush completions must be consumed first.
 * progress() may return both an error and nonzero count: consume all returned
 * completions. Memory exposed to a peer must additionally remain registered
 * until that peer has relinquished its remote key; local references alone
 * cannot establish remote quiescence. Memory registration currently takes host
 * buffers only. Device-memory support requires an explicit interface extension.
 *
 * CPU and future GPU implementations share connection/registration semantics,
 * but not queue layout or host/device completion mechanisms. No verbs types
 * escape this interface. A future GPU extension must explicitly negotiate its
 * memory and execution capabilities rather than accept a CPU pointer as VRAM.
 */
struct crt_native_ep;
struct crt_native_mr;
struct crt_native_group;

enum crt_native_access {
	CRT_NATIVE_REMOTE_READ  = 1,
	CRT_NATIVE_REMOTE_WRITE = 2,
};

enum crt_native_opcode {
	CRT_NATIVE_SEND,
	CRT_NATIVE_RECV,
	CRT_NATIVE_READ,
	CRT_NATIVE_WRITE,
};

struct crt_native_completion {
	void                  *cookie;
	enum crt_native_opcode opcode;
	int                    error;
	size_t                 bytes;
};

/* Exchanged using a trusted bootstrap supplied by the RPC layer. The current
 * native TCP bootstrap provides no peer authentication or encryption.
 * This structure is host-endian and MUST NOT be sent verbatim on the wire.
 */
struct crt_native_address {
	uint32_t qpn;
	uint32_t psn;
	uint16_t lid;
	uint8_t  gid[16];
	uint8_t  mtu;
	uint8_t  read_out, read_in;
	bool     global;
};

struct crt_native_remote_memory {
	uint64_t address;
	uint64_t length;
	uint32_t key;
};

struct crt_native_stats {
	uint64_t send_posts, recv_posts, read_posts, write_posts;
	uint64_t send_batches, recv_batches, signaled_sends, inline_sends;
	uint64_t cq_polls, cqes, logical_completions;
};

struct crt_native_config {
	const char              *device;
	uint8_t                  port;
	int                      gid_index;
	unsigned                 depth;
	unsigned                 signal_interval; /* zero selects eight SENDs */
	unsigned                 cq_poll_batches; /* zero selects four batches of 32 */
	unsigned                 inline_size;
	bool                     stats;
	unsigned                 rdma_read_depth; /* zero selects 16, capped by hardware */
	/* Optional owner-serialized CQ group, with a worker-safe wake notification. */
	struct crt_native_group *group;
	void (*wake)(void *);
	void *wake_arg;
};

#define CRT_NATIVE_BATCH 32
struct crt_native_post {
	enum crt_native_opcode                 opcode;
	struct crt_native_mr                  *mr;
	size_t                                 offset, length;
	const struct crt_native_remote_memory *remote;
	size_t                                 remote_offset;
	void                                  *cookie;
};

struct crt_native_ops {
	const char *name;
	/* Optional shared polling domain. Each endpoint retains a private PD.
	 * group_progress polls once and routes completions to endpoint queues;
	 * endpoint progress consumes those queues without polling hardware again.
	 * All group/endpoint operations are serialized by the same owner.
	 */
	int (*group_create)(const struct crt_native_config *, unsigned, struct crt_native_group **);
	int (*group_progress)(struct crt_native_group *);
	int (*group_destroy)(struct crt_native_group *);
	void (*wake)(struct crt_native_ep *);
	void (*get_stats)(struct crt_native_ep *, struct crt_native_stats *);
	/* Maximum combined outstanding SEND/RECV/READ/WRITE operations. */
	unsigned (*capacity)(struct crt_native_ep *);
	/* Failed opens may return a partial endpoint when cleanup also fails.
	 * Only retry close on that handle; it must remain reachable until closed.
	 */
	int (*open)(const struct crt_native_config *, struct crt_native_ep **);
	int (*address)(struct crt_native_ep *, struct crt_native_address *);
	int (*connect)(struct crt_native_ep *, const struct crt_native_address *);
	int (*register_memory)(struct crt_native_ep *, void *, size_t, unsigned,
			       struct crt_native_mr **, struct crt_native_remote_memory *);
	int (*deregister_memory)(struct crt_native_mr *);
	int (*post)(struct crt_native_ep *, enum crt_native_opcode, struct crt_native_mr *, size_t,
		    size_t, const struct crt_native_remote_memory *, size_t, void *);
	/* Homogeneous linked batch, at most CRT_NATIVE_BATCH entries. A partial
	 * prefix may be accepted even on error; only that prefix owns buffers.
	 * The caller retains every unaccepted request and must not repost the
	 * accepted prefix. No callback is generated for a rejected entry.
	 */
	int (*post_batch)(struct crt_native_ep *, const struct crt_native_post *, unsigned,
			  unsigned *accepted);
	int (*progress)(struct crt_native_ep *, struct crt_native_completion *, unsigned,
			unsigned *);
	int (*close_begin)(struct crt_native_ep *);
	/* After close_begin and after all local completions have been consumed,
	 * destroy the endpoint's remote-access path. Success guarantees the peer
	 * can no longer DMA through registrations owned by this endpoint. Keep
	 * registrations/PD reachable for subsequent deregistration; return EBUSY
	 * while local operations remain. Idempotent; failed teardown is retryable.
	 * A backend sharing protection domains must revoke every path, not merely
	 * destroy one QP. A timeout or successful close_begin is NOT this guarantee.
	 */
	int (*quiesce)(struct crt_native_ep *);
	int (*close)(struct crt_native_ep *);
};

extern const struct crt_native_ops crt_native_verbs_ops;

#endif
