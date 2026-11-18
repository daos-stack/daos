# Testing the experimental doca+rc provider

The CPU implementation uses libibverbs. Keep Mercury installed: the existing
providers and fallback serialization still build. A supported Linux DAOS build
environment, libibverbs development headers/library, active RDMA port and adequate
locked-memory limits are required. The tests do not change system configuration.

## Build

From the DAOS repository root, add the option to your usual SCons command:

```sh
scons -j8 --build-deps=yes CART_NATIVE_RC=yes install native_cart_smoke native_header_test
```

The smoke binary installs to `$PREFIX/bin/native_cart_smoke` (`./install/bin` with
the default prefix). Rebuild all CaRT consumers when toggling this option because
it changes private request storage. A build without it rejects native contexts
with `-DER_NOTSUPPORTED`, rather than silently selecting Mercury.

Run `./build/release/gcc/src/cart/native_header_test` for the versioned native CaRT
header codec regression test (adjust the path for your build/compiler options). Both peers
must run this branch's native wire version 3 and bootstrap hello version 2.
Older peers are rejected: wire version 3 adds bounded RELEASE footers, while
bootstrap hello version 2 negotiates RDMA READ capabilities.

Optional endpoint-only test, before testing CaRT:

```sh
make -C src/cart/native smoke
./src/cart/native/build/verbs_smoke mlx5_0 1
# RoCE: append a valid local GID table index, for example 3.
```

## Real CaRT RPC and bulk test

On both hosts, set the fabric interface appropriate to that host. The HCA, port
and local GID are discovered from it. Ensure
the installed libraries and their dependencies are on your normal loader path.

```sh
export D_PROVIDER=doca+rc
export D_INTERFACE=ib0
export D_PORT=31416
```

On the server, run and leave it running:

```sh
./install/bin/native_cart_smoke server
```

Copy its printed `doca+rc://IP:PORT` URI into the client command on the other host:

```sh
./install/bin/native_cart_smoke client doca+rc://SERVER_IPV4:31416
```

The client checks 1024 RPCs, including 512 4-KiB bulk GET/PUT round trips and
sequence/payload verification, then checks a CaRT error reply. Any failed check
exits unsuccessfully. After the client succeeds, interrupt the server with Ctrl-C
to test teardown. You may also run both processes on one HCA-capable host first.
This test uses public CaRT APIs and real endpoints; it is distinct from the
portable mock-transport test suite.

## DAOS configuration

Merge the provider selection into your existing server YAML; preserve its normal
storage, control-plane and engine configuration:

```yaml
provider: doca+rc
engines:
  - fabric_iface: ib0
    fabric_iface_port: 31416
```

The agent supplies each client with its selected local fabric interface/domain.
Shared CaRT initialization resolves these hints for both clients and engines;
no `CRT_NATIVE_DEVICE`, `CRT_NATIVE_PORT` or `CRT_NATIVE_GID_INDEX` is required.
Server HCA names and GID indices are not copied to clients: selection is local.
Allow TCP bootstrap ports `fabric_iface_port + context_index` between peers.
This is a trusted-fabric bootstrap without authentication/encryption; configured
fabric authentication keys are rejected.

Native tuning variables, independently on each process:

| Variable | Default | Meaning |
| --- | --- | --- |
| `CRT_NATIVE_DEVICE` | automatic | Optional HCA override; otherwise use the agent/domain hint and interface mapping |
| `CRT_NATIVE_PORT` | automatic (0) | Discover the active physical port for the selected interface |
| `CRT_NATIVE_GID_INDEX` | automatic (-2) | Match the interface IPv4 address, prefer RoCEv2; -1 explicitly selects IB LID addressing |
| `CRT_NATIVE_SIGNAL_INTERVAL` | 8 | SEND completion interval within each linked batch; 1 signals every SEND |
| `CRT_NATIVE_CQ_BATCHES` | 4 | Maximum 32-CQE batches drained per progress call, 1–16 |
| `CRT_NATIVE_INLINE_SIZE` | 256 | Requested hardware-inline SEND bytes; 0 disables; unsupported sizes fall back |
| `CRT_NATIVE_STATS` | 0 | Print endpoint protocol counters at teardown when set to 1 |
| `CRT_NATIVE_RDMA_READ_DEPTH` | 16 | Requested READ concurrency, capped by local hardware and peer responder credits |
| `CRT_NATIVE_RECEIVES` | 64 | Receives per peer, 2–8192 |
| `CRT_NATIVE_FRAME_SIZE` | 65536 | Maximum frame bytes, 128–1048576 |
| `CRT_NATIVE_MAX_PEERS` | 128 | Peer slots per context, 1–4096 |

Test small RPC/bulk traffic before attempting DAOS workloads. Current limits
include bounded eager bodies, no bound/forwarded bulk, no automatic reconnection
and unqualified SWIM/collectives. A passing smoke test does not qualify a DAOS
deployment. Report the first failing API/status and server/client logs when
testing unsupported or unvalidated paths.

After repeated client or fio runs, verify that a new `daos cont list POOL` still
works without restarting the server. Progress retires failed peer hardware after
DMA, receive leases, requests and bulk consumers drain. The portable connection
test covers 128 sequential clients with only two server peer slots, checking that
endpoint and registered-memory counts return to baseline between clients.

The portable network suite runs both grouped and standalone polling modes. Its
idle-loop syscall counters check that connected idle peers generate no `accept`
or `MSG_PEEK` calls, and that TCP polling is periodic rather than per progress
call. It also exercises 70 concurrently ready peers across the 32-peer scheduling
budget and a bitmap word boundary. The verbs suite verifies interleaved endpoint
completions on one shared CQ, retained registration ownership until delivery,
group lifetime, and flushing after a shared poll error.

For performance comparisons, rebuild/install both client and server, restart the
server, and use the same fio job file, CPU/NUMA placement, object class, dataset
and warm-up settings as the Mercury/UCX baseline. Record IOPS, latency percentiles
and client/server CPU use. Portable tests establish correctness properties, not
an IOPS target; actual throughput requires the real-HCA fio run.

Bootstrap hello version 2 advertises initiator and responder RDMA READ credits.
Both peers must use this version; older bootstrap versions are rejected.

## Protocol performance controls

Linked SEND/RECV batches contain at most 32 work requests. SEND markers use the
configured interval and always include the batch tail. A rejected tail marker
forces QP error/flush so accepted unsignaled buffers cannot be stranded. READ and
WRITE work requests remain individually signaled; their completion is never
inferred from a later SEND. A scalar posting path remains for other backends.

RELEASE IDs are batched or attached to available request/reply space, with no
extra wait for a carrier. Idle connections send standalone batches. Only IDs
already released by the bulk session are eligible: receiving an RPC reply does
not itself authorize releasing bulk keys or prove completion on the bulk QP.

Set `CRT_NATIVE_STATS=1` on client and server to report counters at endpoint
teardown. Endpoint statistics include work requests, post batches, signaled and
inline SENDs, hardware CQEs and delivered logical completions. Shared CQ counters
are reported once per group. RPC statistics count selected request/reply/control
frames, standalone and piggybacked release IDs, and retained/copied early replies.
Selected frames can include canceled, unposted carriers and retransmitted ACKs;
use the posting counters for hardware-accepted work. For instance, compare SENDs
per posting batch and signaled SENDs per accepted SEND before comparing IOPS.

Useful experiments change one variable at a time: signal interval 1 versus 8,
inline size 0 versus 256, CQ batches 1 versus 4, and READ depth 1 versus 16. Apply
these variables through engine `env_vars` and the client shell. Upgrade and
restart all native peers together after the wire-format change; do not reformat
storage. No real-HCA throughput gain is implied by the portable regression suite.

## Single-request paths and frame-size statistics

Single work requests bypass linked-list posting scratch and selective-signaling
bookkeeping. A one-entry batch, or a batch with only one admission slot left,
uses the same scalar path. A lone channel SEND also bypasses temporary batch
queueing; backpressure retains the unposted frame in the normal retry queue.

Successful ordinary CQEs are delivered directly. Only marker expansion and
error handling use the logical-completion queue. Unconsumed raw CQEs remain
owned by the endpoint across small caller output buffers, preserving ordering
and registration lifetime when markers and ordinary completions are mixed.

`CRT_NATIVE_STATS=1` adds `inline_requested` and `inline_limit` (the actual QP
capacity), accepted-operation path counts `single_posts`/`batch_posts`, and
`direct_completions`/`queued_completions`. The latter two sum to `logical`.

SEND-size collection is enabled only with statistics, so disabled instrumentation
avoids histogram/min/max updates. Sizes include the entire native/CaRT frame and
any attached RELEASE footer. `send_bytes`, `send_min` and `send_max` accompany
these disjoint buckets:

| Counter | SEND length in bytes |
| --- | --- |
| `send_le64` | 1–64 |
| `send_le128` | 65–128 |
| `send_le256` | 129–256 |
| `send_le512` | 257–512 |
| `send_le1024` | 513–1024 |
| `send_le4096` | 1025–4096 |
| `send_gt4096` | 4097 and above |

Use the negotiated capacity and size distribution together to explain inline
usage. A software-eager 4-KiB payload can exceed both 4 KiB on the wire and the
hardware inline limit. For performance comparisons, keep all existing tuning
settings unchanged, collect statistics in a separate diagnostic run, and compare
both throughput and latency against the same job count and queue depth.

## Automatic native fabric selection

Discovery runs at initialization, using Linux `/sys/class/infiniband` metadata.
It selects an active port associated with the configured netdev. On Ethernet it
matches an IPv4-mapped GID to the exact local interface address and prefers the
RoCEv2 entry over the RoCEv1 entry. Link-local and other-interface GIDs are not
selected automatically. Native InfiniBand uses LID addressing by default.

For example, `fabric_iface: eth1` on a host mapping eth1 to mlx5_2/1 with IPv4
RoCEv2 GID 3 resolves that tuple without environment exports. HCA domain hints
from the agent constrain discovery, including `device:port` domains. An explicit
`CRT_NATIVE_DEVICE` takes precedence over the domain; an explicit port takes
precedence over a domain suffix. Explicit GID indices remain available for
experiments using a different local GID.

Equally suitable device/port mappings fail with an ambiguity diagnostic rather
than choosing an arbitrary HCA. The INFO log records the selected interface,
HCA, port and GID. Existing overrides still win: remove old engine `env_vars`
entries and unset old shell exports when testing automatic selection. Preserve
all unrelated server/agent settings. No new agent YAML or attach-info protocol
fields are needed; the existing interface/domain hints are sufficient.
