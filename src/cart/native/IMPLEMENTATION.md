# Native RC provider implementation checklist

The target is a **native CaRT RPC provider** selected by `doca+rc`, keeping
existing Mercury providers. The CPU datapath uses open-source libibverbs as
agreed; DOCA Verbs/GPUNetIO will be separate implementations. DC is deferred.

The provider is not usable until the CaRT bridge and selection gates below are
implemented. Unit tests of individual modules are not DAOS-provider validation.

| Gap | Status | Required result |
| --- | --- | --- |
| 1. Endpoint exchange | Implemented, hardware-free tests pass | Versioned endian-safe hello, peer-limit negotiation, bounded fragmented socket exchange, explicit incompatibility errors |
| 2. Eager RPC matching | Implemented, hardware-free tests pass | Bounded generational request IDs, matching, separate request/reply queues, delayed replies, cancellation and send-buffer ownership without per-RPC allocation |
| 2a. Registered channel driver | Implemented, hardware-free full-duplex tests pass | Registered buffer pools, receive replenishment, bounded transport progress, retry under posting backpressure; shutdown drains accepted I/O before deregistration |
| 3. Native bulk engine | Implemented, hardware-free tests pass | Endian-safe descriptors, NIC-enforced read/write permissions, export leases, bounded SGL slicing, chunk submission/retry, cancellation and completion-driven release |
| 3a. Native payload serialization | Implemented, hardware-free tests pass | Versioned bounded codec, canonical signed/unsigned scalars, borrowed byte/string views, native bulk descriptors; direct encoding into registered SEND frames and direct RX completion when SEND has finished |
| 3b. CaRT serializer dispatch | Implemented source and adapter unit tests | Existing crt_proc callbacks dispatch to a scoped native cursor or Mercury; common/collective headers encode field by field; borrowed strings/iovs and owned array metadata support decode-error FREE cleanup; explicit connection-specific bulk binding hook |
| 3c. Receive leases | Implemented, threaded tests pass | Retain RX through asynchronous consumers; worker-safe release queues repost on progress owner; shutdown cannot deregister a retained slab |
| 3d. CaRT bulk/runtime lifecycle binding | Pending | Supply connection-specific crt_bulk wrappers and export-lease acknowledgment/quiescence; acquire/release RX leases in actual CaRT dispatch/destruction; HLC/routing/deadline validation before dispatch |
| 4. CaRT bridge | Pending | Listener and endpoint cache; context/progress, dispatch and completion integration; groups, context tags, SWIM and collective compatibility |
| 5. Cancellation/teardown | Pending | Request deadlines, exact-once completion, native connection errors, remote quiescence, callback-safe shutdown |
| 6. Build/provider selection | Pending | Optional build, control-plane discovery, YAML `doca+rc`, explicit unsupported-build errors, no silent fallback |
| 7. End-to-end validation | Pending, user will run hardware tests | Linux compile/link, two-node RPC and DAOS functional/failure tests; RPC-rate and CPU comparisons |

Endpoint exchange currently takes an already connected, authenticated stream
socket owned by the caller. Creating/listening/authenticating/discovering peers
is part of gap 4; this helper does not claim to implement that functionality.
