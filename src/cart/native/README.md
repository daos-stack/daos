# Experimental native CaRT transport

Build with `CART_NATIVE_RC=yes` to enable the `doca+rc` CaRT provider. Existing
Mercury providers remain available. The CPU datapath uses open-source libibverbs;
it does not use DOCA SDK objects or GPU submission yet. Private backend operations
keep future DOCA/GPUNetIO implementations separate from the RPC protocol.

See [TESTING.md](TESTING.md) for build, YAML selection and two-process CaRT tests.
See [IMPLEMENTATION.md](IMPLEMENTATION.md) for implementation status.

## Architecture

- `transport.h` / `verbs.c`: host RC endpoints, registrations, NIC permissions,
  preallocated operation records, CQ batching and explicit DMA quiescence.
- `net.h`: nonblocking IPv4 TCP endpoint/limit negotiation, separate RC control
  and bulk endpoints, receive preposting and a READY barrier. TCP carries
  bootstrap/liveness only; RPC payloads use RC. Simultaneous connects are supported.
  Failed connections release hardware resources during progress; uncached accepted
  clients also release their peer slots after asynchronous consumers have drained.
- `rpc.h` / `channel.h`: registered eager frames, generational matching, independent
  request/reply/control reserves, backpressure, deadline heap and RX leases.
- `proc.h` / `cart_proc.h`: bounded, versioned big-endian encoding using existing
  CaRT callbacks; native scoped cursors and Mercury cursors stay distinct.
- `bulk.h` / `bulk_session.h` / `cart_bulk.h`: registered SGLs, connection-bound
  keys, export/import leases, RELEASE acknowledgments, bounded registration cache
  and chunked GET/PUT. Workers submit/release; the owner registers/posts/polls.
- `crt_native.c`: actual CaRT lookup, context progress, request lifecycle and
  dispatch. Validates opcode, destination rank/tag, HLC and deadline. Application
  handlers/completions run outside the transport mutex. Pending submissions and
  active-request removal use constant-time links.

Encoding writes into registered SEND frames. Strings/iovs decode as borrowed RX
views held through CaRT RPC destruction; early replies require a copy until local
SEND completion. Array metadata is allocated. Bulk payloads use RDMA without a
codec copy. Cancellation retains accepted buffers until completion; published
keys remain pinned until peer acknowledgment or proven endpoint quiescence.
Context destruction progresses transport flushes and deferred callbacks for up to
one second before returning busy. Callbacks run outside the transport lock.
Busy/failed cleanup preserves ownership for retry, including forced context destroy;
the deadline never permits freeing memory still owned by the NIC or application.

## Limits and validation

This is experimental source integration. Portable tests and header syntax checks
do not constitute a successful Linux link, real-HCA test or DAOS qualification.

- Trusted fabric only: no bootstrap authentication/encryption. Nonempty fabric
  auth keys fail explicitly. IPv4 and host buffers only; explicit GID index for RoCE.
- Bounded eager messages; oversized bodies return truncation. Use bulk for large
  data. Eager fragmentation/rendezvous is not implemented.
- Bound bulk and forwarding decoded keys to another peer are unsupported.
  Collective header/dispatch wiring does not establish collective compatibility.
- Failed cached peers do not reconnect automatically; recreate their context.
  Cached address shells stay stable until context destruction, while their failed
  connection's QPs, CQs, protection domains and registered buffers retire earlier.
- Native one-way completion waits for an internal server release acknowledgment.
- DC, GPU submission, selective signaling, full recovery, SWIM/collective
  qualification and comparative benchmarks remain future work.

```sh
cd src/cart/native
make test
make test BUILD=build-asan \
    CFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer'
```

Tests cover state, ownership, faults and real loopback TCP with mock RC endpoints.
The TCP test needs local socket permissions. Test compatibility/fake headers are
excluded from production builds and do not validate the real verbs ABI or ordering.
