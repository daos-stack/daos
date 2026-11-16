# Native RC provider implementation checklist

The target is a **native CaRT RPC provider** selected by `doca+rc`, keeping
existing Mercury providers. The CPU datapath uses open-source libibverbs as
agreed; DOCA Verbs/GPUNetIO will be separate implementations. DC is deferred.

The provider is not usable until the CaRT bridge and selection gates below are
implemented. Unit tests of individual modules are not DAOS-provider validation.

| Gap | Status | Required result |
| --- | --- | --- |
| 1. Endpoint exchange | Implemented, hardware-free tests pass | Versioned endian-safe hello, peer-limit negotiation, bounded fragmented socket exchange, explicit incompatibility errors |
| 2. RPC engine | Pending | Request IDs, matching, eager request/reply, receive replenishment and independent reply credits without per-RPC allocation |
| 3. Bulk/codec | Pending | Native bulk descriptors, local/remote permissions and lifetimes, CaRT payload serialization independent of HG handles |
| 4. CaRT bridge | Pending | Listener and endpoint cache; context/progress, dispatch and completion integration; groups, context tags, SWIM and collective compatibility |
| 5. Cancellation/teardown | Pending | Request deadlines, exact-once completion, native connection errors, remote quiescence, callback-safe shutdown |
| 6. Build/provider selection | Pending | Optional build, control-plane discovery, YAML `doca+rc`, explicit unsupported-build errors, no silent fallback |
| 7. End-to-end validation | Pending, user will run hardware tests | Linux compile/link, two-node RPC and DAOS functional/failure tests; RPC-rate and CPU comparisons |

Endpoint exchange currently takes an already connected, authenticated stream
socket owned by the caller. Creating/listening/authenticating/discovering peers
is part of gap 4; this helper does not claim to implement that functionality.
