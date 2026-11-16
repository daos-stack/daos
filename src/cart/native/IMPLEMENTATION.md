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
| 3b. CaRT codec/bulk binding | Pending | Route existing CaRT proc callbacks, common/collective headers and bulk wrappers through the native codec; retain RX for asynchronous dispatch; bind export leases to actual RPC acknowledgment/connection quiescence; preserve CaRT encode/decode/free semantics |
| 4. CaRT bridge | Pending | Listener and endpoint cache; context/progress, dispatch and completion integration; groups, context tags, SWIM and collective compatibility |
| 5. Cancellation/teardown | Pending | Request deadlines, exact-once completion, native connection errors, remote quiescence, callback-safe shutdown |
| 6. Build/provider selection | Pending | Optional build, control-plane discovery, YAML `doca+rc`, explicit unsupported-build errors, no silent fallback |
| 7. End-to-end validation | Pending, user will run hardware tests | Linux compile/link, two-node RPC and DAOS functional/failure tests; RPC-rate and CPU comparisons |

Endpoint exchange currently takes an already connected, authenticated stream
socket owned by the caller. Creating/listening/authenticating/discovering peers
is part of gap 4; this helper does not claim to implement that functionality.
