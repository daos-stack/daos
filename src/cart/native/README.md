# Experimental native CaRT transport

This is an initial transport bring-up implementation, **not a selectable CaRT
provider**. The normal DAOS SCons build and Mercury transport are unchanged.
It does not yet run DAOS RPCs or replace Mercury serialization. Do not set
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
registers those frames, posts receives and drives the transport. The CaRT bridge
must supply authenticated connections, payload codecs and handler integration.

`bulk.h` provides connection-bound SGL registration, endian-safe remote memory
descriptors, export leases and bounded chunked READ/WRITE execution. A dedicated
bulk progress owner must route its completions: do not submit bulk operations
onto the RPC channel's exclusively owned endpoint. Canceling a transfer stops
unsubmitted chunks and retains local registrations until accepted chunks finish.
Releasing an exported remote key requires a peer acknowledgment or established
remote quiescence. The CaRT adapter does not yet provide that lifecycle binding;
a local RPC timeout must never be treated as such an acknowledgment.

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
