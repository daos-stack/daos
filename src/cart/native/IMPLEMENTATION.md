# Native RC provider implementation checklist

The target is a **native CaRT RPC provider** selected by `doca+rc`, keeping
existing Mercury providers. The CPU datapath uses open-source libibverbs as
agreed; DOCA Verbs/GPUNetIO will be separate implementations. DC is deferred.

The provider is selectable in an enabled build. CaRT bridge/selection source is
implemented; Linux link and real-HCA qualification remain outstanding. Portable
module tests are not DAOS-provider validation.

| Gap | Status | Required result |
| --- | --- | --- |
| 1. Endpoint exchange | Implemented, hardware-free tests pass | Versioned endian-safe hello, peer-limit negotiation, bounded fragmented socket exchange, explicit incompatibility errors |
| 2. Eager RPC matching | Implemented, hardware-free tests pass | Bounded generational request IDs, matching, separate request/reply queues, delayed replies, cancellation and send-buffer ownership without per-RPC allocation |
| 2a. Registered channel driver | Implemented, hardware-free full-duplex tests pass | Registered buffer pools, receive replenishment, bounded transport progress, retry under posting backpressure; shutdown drains accepted I/O before deregistration |
| 3. Native bulk engine | Implemented, hardware-free tests pass | Endian-safe descriptors, NIC-enforced read/write permissions, export leases, bounded SGL slicing, chunk submission/retry, cancellation and completion-driven release |
| 3a. Native payload serialization | Implemented, hardware-free tests pass | Versioned bounded codec, canonical signed/unsigned scalars, borrowed byte/string views, native bulk descriptors; direct encoding into registered SEND frames and direct RX completion when SEND has finished |
| 3b. CaRT serializer dispatch | Implemented source and adapter unit tests | Existing crt_proc callbacks dispatch to a scoped native cursor or Mercury; common/collective headers encode field by field; borrowed strings/iovs and owned array metadata support decode-error FREE cleanup; explicit connection-specific bulk binding hook |
| 3c. Receive leases | Implemented, threaded tests pass | Retain RX through asynchronous consumers; worker-safe release queues repost on progress owner; shutdown cannot deregister a retained slab |
| 3d. Bulk ownership and acknowledgments | Implemented, portable tests pass | Connection-bound generational export/import ledger; publish/abort transactions; independent RELEASE wire queue; explicit transport quiescence before revoking exports after failure |
| 3e. CaRT bulk API binding | Implemented source and adapter tests | Concrete decoded crt_bulk wrappers, bounded registration cache, worker submission/release queues, native GET/PUT routing, completion-driven references, abort routing and deadline checks; context must call binding progress |
| 3f. CaRT RPC/runtime lifecycle binding | Implemented source | Peer channels/sessions/bindings, RX ownership through actual RPC destruction, HLC/routing/deadline checks; bound/forwarded bulk explicitly unsupported |
| 4. CaRT bridge | Implemented source and loopback runtime tests | Nonblocking listener/endpoint cache, hello/READY negotiation, context progress, handler/completion dispatch, group rank and tag routing; SWIM/collective qualification pending |
| 5a. Native deadlines and cancellation | Implemented, portable tests pass | Bulk deadline/cancel with accepted-DMA drain; preallocated RPC deadline heap; bounded expiration; no posting expired queued/retry frames; exact-once completion waits for SEND CQE |
| 5b. CaRT cancellation/teardown integration | Implemented source | Deadline/abort mapping, deferred callbacks outside transport lock, CaRT request tracking/completion; busy teardown preserves contexts even with force |
| 6. Build/provider selection | Implemented source | Optional CART_NATIVE_RC build, protocol discovery, YAML doca+rc, explicit unsupported-build errors, no silent fallback |
| 7. End-to-end validation | Pending, user will run hardware tests | Linux compile/link, two-node RPC and DAOS functional/failure tests; RPC-rate and CPU comparisons |

`net.h` now owns TCP listener/connect/hello/READY/liveness and dedicated RC endpoint
pairs. Bootstrap is unauthenticated and requires a trusted fabric; nonempty fabric
auth keys fail explicitly. Bounded eager messages, IPv4, host buffers and context
recreation after connection failure are current limits. See TESTING.md for the
public CaRT API RPC/bulk smoke executable and README.md for the full limitations.
