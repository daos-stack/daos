# Experimental native CaRT transport

This is an initial transport bring-up implementation, **not a selectable CaRT
provider**. The normal DAOS SCons build and Mercury transport are unchanged.
It does not yet run DAOS RPCs. Native serialization is an explicit private path;
Mercury serialization remains the default. Do not set
`CRT_PHY_ADDR_STR=verbs` expecting this code to be selected.

## Implemented

- Private backend operations with no exposed libibverbs, DOCA, or CUDA types.
- CPU host-memory RC verbs endpoint creation, address export, and connection.
- SEND/RECV and one-sided READ/WRITE using registered buffers.
- Bounded outstanding work with `EAGAIN` backpressure and completion cookies.
- Preallocated operation state with constant-time free-list allocation and
  recycling; bounded CQ polling batches of up to 32 entries. No allocation is
  performed on the posting or completion paths. All accepted work remains
  signaled; selective signaling and chained posting are not implemented.
- Registration lifetime tracking, error/flush completion handling, and busy close.
- A versioned, big-endian request/reply envelope with strict frame validation.
- Hardware-free fault/ownership tests and a separate real-HCA smoke executable.

DC is deferred. These optimizations reduce local software work but have not been
benchmarked on an RDMA device and establish no end-to-end RPC-rate improvement.

The endpoint is owned by one serialized execution context. Posting, polling,
registration, deregistration, and closing must not run concurrently on it. Every
accepted work request is signaled. Applications must consume returned completions
even when `progress()` also reports an error. A polling failure does not establish
buffer safety: if completions cannot be recovered, the endpoint and registered
buffers must remain alive. This intentionally favors buffer safety over forced
cleanup. Remote-key lifetimes need a lease/quiescence protocol at the RPC layer.

## Local validation

The test double in `tests/fake` is only for state and ownership tests; it does not
validate the real provider, connection transitions, RDMA ordering, or real API ABI.
It is excluded from production builds.

```sh
cd src/cart/native
make test
make test BUILD=build-asan \
    CFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer'
```

## Linux build and real-HCA test

Install the system C toolchain and libibverbs development headers, then:

```sh
cd src/cart/native
make
make smoke
# InfiniBand, port 1 (requires an active fabric):
./build/verbs_smoke mlx5_0 1
# RoCE: explicitly select a valid local GID table index:
./build/verbs_smoke mlx5_0 1 3
```

The smoke test connects a QP to itself and verifies SEND/RECV and RDMA WRITE/READ
through the HCA. It is not a two-node test, CaRT test, or performance benchmark.
The process needs appropriate RDMA device permissions and locked-memory limits.
No privileged system configuration is changed by the test.

## Remaining before CaRT can select this backend

The per-commit status is tracked in [IMPLEMENTATION.md](IMPLEMENTATION.md).
Endpoint hello exchange, a bounded eager RPC engine, and a registered channel
driver are now implemented as private modules. The channel tests simulate full
duplex communication with injected posting backpressure. They do not run CaRT
handlers or DAOS operations, and they do not validate a real verbs provider.

`bootstrap.h` exchanges endpoint parameters over a caller-owned socket;
`rpc.h` matches requests/replies and owns preallocated frames; `channel.h`
registers those frames, posts receives and drives the transport. `proc.h` supplies
an allocation-free native payload codec. `cart_proc.h` binds existing CaRT proc
callbacks to the codec; the normal CaRT SCons target includes this CPU-only
binding without introducing a verbs dependency. The CaRT bridge must still
supply authenticated connections, connection-specific bulk bindings, and actual
context/handler dispatch. No context selects this path automatically.

`bulk.h` provides connection-bound SGL registration, endian-safe remote memory
descriptors, export leases and bounded chunked READ/WRITE execution. A dedicated
bulk progress owner must route its completions: do not submit bulk operations
onto the RPC channel's exclusively owned endpoint. Canceling a transfer stops
unsubmitted chunks and retains local registrations until accepted chunks finish.
Releasing an exported remote key requires a peer acknowledgment or established
remote quiescence. `bulk_session.h` supplies that ledger and `cart_bulk.h` supplies
the CaRT bulk binding described below. The context bridge must still instantiate
and progress them. A local RPC timeout must never act as an acknowledgment.

## Serialization and copy ownership

`crt_native_rpc_call_encoded()` and `crt_native_rpc_reply_encoded()` invoke a
synchronous encoder directly on the payload region of the registered SEND slab.
Initialize a stack `crt_native_proc` on that region, serialize fields, then return
the length from `crt_native_proc_finish()`. An encoder failure queues no frame;
a failed reply encoder leaves the handler token available for retry. Encoder
callbacks must not reenter the RPC engine or retain the buffer. Accepted frames
remain owned by the transport until SEND completion as before.

The codec uses an eight-byte `CRTP`/version-1 prefix, fixed-width big-endian
integers (including signed two's-complement values), one-byte canonical booleans,
and no alignment padding. Strings and byte fields have u64 byte counts; strings
include their terminating NUL (zero count denotes NULL). Bulk fields contain a
u32 descriptor length and the existing versioned native descriptor, with zero
denoting NULL. Lengths are bounded before access, errors remain sticky, decode
requires exact consumption, and malformed/trailing input is rejected. Raw fields
are opaque bytes, never an excuse to transmit C structs with native layout.

| Path | Payload copies performed by this layer |
| --- | --- |
| Encode scalars/headers | Direct stores into the final registered frame |
| Encode existing application strings/bytes | One copy into the final frame |
| Produce bytes using `u64()` + `span()` | None; producer writes the reserved final span |
| Decode request strings/bytes | None; borrowed views into RX |
| Decode reply after SEND completion | None; callback borrows RX before repost |
| Reply received before SEND completion | One copy into the preallocated response slab |
| Bulk payload | No codec copy; only descriptors are serialized, data uses RDMA |

This is not universal end-to-end zero copy. Borrowed views expire when the handler
or completion callback returns unless its receive buffer is retained (see below).
`FREE` only resets borrowed views and does not free
their backing memory. It must not release bulk export leases; those remain the
RPC lifecycle owner's responsibility. Large fields should use the bulk path;
there is no eager fragmentation/rendezvous fallback yet. No performance claim
follows from the hardware-free codec tests.

## Existing CaRT callback binding

`crt_native_cart_run()` runs an existing `crt_proc_cb_t` with a stack cursor.
Public `crt_proc_*` entry points recognize that cursor by exact equality in a
thread-local scope stack, otherwise forwarding to their Mercury implementations.
No Mercury private structs are inspected, and there is no pointer tagging.
Nested native and Mercury processing is supported; cursors cannot outlive the
call or move to another thread. The binding converts errno into CaRT errors.

Strings and iovs decode as borrowed views. Borrowed iov capacity equals its
transmitted length, so callers cannot write into a sender-advertised capacity
beyond the actual RX data. Rank arrays and SGL metadata require allocation;
payload bytes remain borrowed. FREE releases that metadata, clears views and
also works during generated callback error unwinding on the same decode proc.
Use FREE only on objects decoded by this binding.

`crt_native_cart_payload()` combines the common/collective headers with a known
registered body schema. Native headers encode individual fields rather than C
struct padding; Mercury's existing wire layout is preserved. The caller supplies
encode header values and preallocated request/output objects. The later context
bridge must validate HLC, routing, identity and deadlines before dispatch.

Non-NULL bulk handles require the connection-specific bulk callback supplied to
`crt_native_cart_run()`/`payload()`. That hook owns local-handle lookup, decoded
wrappers, and export leases. Absent a hook, only NULL bulk handles are supported;
non-NULL handles fail explicitly with `-DER_NOSYS`. A Mercury bulk handle is never
mistaken for a native export. `crt_native_cart_bulk_proc()` now implements this
callback; installing it on real peer contexts remains part of the context bridge.

Portable adapter tests compile against real CaRT API/types with Mercury fallback
test doubles. Test-only empty Boost and byteswap includes permit these tests on
macOS; they do not expand the generated Boost schemas or replace production
dependencies. Full CaRT/Mercury compilation remains a Linux integration check.

## Receive ownership across asynchronous dispatch

During a receive callback, call `crt_native_channel_retain(channel, view, size,
&lease)` on the progress owner before handing a borrowed view to a worker. Leases
are caller-owned, initially zero, and must not be copied. Each successful retain
requires exactly one `crt_native_channel_release(&lease)`. Release may run on a
worker: it queues a buffer index, and the progress owner later reposts the RX.
QP access and repost retries remain on the progress thread. Shutdown/destroy
preserve registered slabs until all NIC work and all leases have drained.

Only views backed by the currently delivered RX can be retained. Early replies
copied to the RPC response slab are not RX-backed; consumers needing asynchronous
ownership of those responses must make an owned copy. Retaining an RX is separate
from owning a server request/reply token; release both lifetimes correctly. The
actual CaRT dispatcher still needs to attach leases to its RPC destruction path.

## Bulk publication, acknowledgments and CaRT transfers

Create one `crt_native_bulk_session` on the dedicated bulk endpoint per peer,
and bind the control channel's RELEASE callback to `crt_native_bulk_session_ack`.
The ACK sender queues `crt_native_channel_queue_release()`. Both peers use the
negotiated ledger capacity; do not share a ledger between endpoints or peers.
RELEASE frames use an independent preallocated queue/slab, so ACKs can progress
with a full request window. Their IDs are generation-tagged and connection-local;
future/unpublished IDs are rejected and stale/duplicate ACKs are harmless.

Create a `crt_native_cart_bulk` cache on the same session/endpoint. Use a stack
`crt_native_cart_bulk_txn` with `crt_native_cart_bulk_proc()` during payload
serialization. After successful encoding, commit its exports before publishing
the frame; abort drafts on earlier encoding/queue errors. Once published, a key
stays pinned until peer RELEASE receipt or successful endpoint quiescence. A
queued acknowledgment or a local timeout is insufficient. A decoder failure
must shut down the peer: unparsed bulk fields may contain additional exports
which cannot safely be located/released by scanning arbitrary payload bytes.

The binding registers local CaRT SGLs with their original RO/WO/RW permissions
and caches a bounded number of registrations. Cache entries retain the original
CaRT wrapper and cannot be evicted while exports or accepted local DMA use them.
Decoded native wrappers expose length/segment count, but never expose remote
virtual addresses as accessible local buffers. `crt_bulk_free()` can run on a
worker; final destruction and import release are queued to binding progress.

`crt_bulk_transfer()` routes decoded native handles to a preallocated GET/PUT
queue. Worker submissions retain the RPC and both bulk handles; registration,
posting, CQ polling and callbacks run on the progress owner. `crt_bulk_abort()`
recognizes native pool membership without interpreting Mercury operation IDs.
Operation IDs are valid until their completion callback, as with other CaRT
bulk operations; callers must synchronize cancellation with completion. Deadlines
are checked before posting and during progress. Cancellation stops unsent chunks,
then waits for accepted DMA before the single completion callback and unpinning.

Progress the control channel and `crt_native_cart_bulk_progress()` independently.
For teardown, stop new work, shut down/drain the bulk session, and retry quiesce
until it succeeds. The verbs implementation destroys the only QP in its private
PD while retaining MRs for subsequent deregistration. Only then may published
exports be revoked without peer ACKs. Drain wrapper releases and destroy the
binding before its session; shut down the control channel before destroying the
session referenced by its RELEASE callback. Live wrappers/transfers/leases keep
destruction busy, and failed deregistration/teardown retains retryable handles.

Forwarding a decoded key to a different peer is unsupported: registrations belong
to the original connection's PD. Bound-handle forwarding and collective relay
need explicit routing/proxy support before claiming collective compatibility.
The native provider is still unselectable; these implemented bulk paths await
the listener/context/dispatch integration and full Linux CaRT validation.

## Remaining integration work

1. Introduce transport-neutral context, address, request, and bulk state alongside
   the current Mercury state. Convert direct HG calls outside `crt_hg.c` and
   `crt_hg_proc.c` to that interface while preserving a Mercury implementation.
2. Add address/connection bootstrap and protocol negotiation. Never transmit the
   C address or memory-descriptor structures verbatim. Validate peer identity
   before exchanging access keys.
3. Add request-ID matching, receive-buffer replenishment, bounded eager messages,
   rendezvous for larger messages, and reply/credit reserves. Reserve receive and
   send capacity independently at the RPC layer to prevent backpressure deadlock.
4. Isolate the existing CaRT payload codec from Mercury handles. Implement native
   bulk-handle serialization, leases, permissions and remote quiescence.
5. Connect incoming messages to existing CaRT request dispatch; preserve credits,
   timeouts, cancellation, group addressing, collectives, and SWIM behavior.
6. Add explicit provider/build selection only after those paths function. Unknown
   or unbuilt providers must fail explicitly; never fall back silently to Mercury.
7. Run two-node RPC/bulk, concurrent contexts, failure/recovery and DAOS tests,
   plus comparison benchmarks against Mercury/UCX.

## DOCA Verbs and GPUNetIO extension

The intended dependency is the open-source NVIDIA-DOCA/gpunetio repository,
inspected at commit `d30aa937093e702c0942b1733a867d5b8d47b697` (reported version
5.0.0). It is not vendored into DAOS or required to build this CPU prototype.

Implement a separate backend operations table for DOCA resources; do not expose
its QP/CQ layout to CaRT. Keep RPC framing, matching, and cache/DAOS policy above
that table. Device buffers, device-side submission/completions, registration
export and memory ordering require an explicit capability/interface extension;
the current CPU interface is not a GPU ABI.

The inspected open implementation supplies GPU one-sided operations, not a full
GPU two-sided RPC path. A future implementation must choose and negotiate either
CPU-driven control messages with GPU bulk transfers or a new GPU mailbox protocol.
It must not assume unsupported full-SDK host bridge functions are available.
