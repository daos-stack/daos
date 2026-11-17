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
must run this branch's native wire version 2; version 1 peers are rejected during
bootstrap because their CaRT payload layout predates upstream's versioned headers.

Optional endpoint-only test, before testing CaRT:

```sh
make -C src/cart/native smoke
./src/cart/native/build/verbs_smoke mlx5_0 1
# RoCE: append a valid local GID table index, for example 3.
```

## Real CaRT RPC and bulk test

On both hosts, set the interface and HCA names appropriate to that host. Ensure
the installed libraries and their dependencies are on your normal loader path.

```sh
export D_PROVIDER=doca+rc
export D_INTERFACE=ib0
export CRT_NATIVE_DEVICE=mlx5_0
export CRT_NATIVE_PORT=1
# RoCE only: choose a valid index locally on EACH host.
# export CRT_NATIVE_GID_INDEX=3
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
    env_vars:
      - CRT_NATIVE_DEVICE=mlx5_0
      - CRT_NATIVE_PORT=1
      # RoCE: add CRT_NATIVE_GID_INDEX=<local index>.
```

For DAOS clients/agents, choose the same provider and their local interface/HCA.
Allow TCP bootstrap ports `fabric_iface_port + context_index` between peers.
This is a trusted-fabric bootstrap without authentication/encryption; configured
fabric authentication keys are rejected.

Native tuning variables, independently on each process:

| Variable | Default | Meaning |
| --- | --- | --- |
| `CRT_NATIVE_DEVICE` | domain setting, or sole HCA | Local verbs device |
| `CRT_NATIVE_PORT` | 1 | HCA port |
| `CRT_NATIVE_GID_INDEX` | -1 | Local GID index; required for RoCE |
| `CRT_NATIVE_RECEIVES` | 64 | Receives per peer, 2–8192 |
| `CRT_NATIVE_FRAME_SIZE` | 65536 | Maximum frame bytes, 128–1048576 |
| `CRT_NATIVE_MAX_PEERS` | 128 | Peer slots per context, 1–4096 |

Test small RPC/bulk traffic before attempting DAOS workloads. Current limits
include bounded eager bodies, no bound/forwarded bulk, no automatic reconnection
and unqualified SWIM/collectives. A passing smoke test does not qualify a DAOS
deployment. Report the first failing API/status and server/client logs when
testing unsupported or unvalidated paths.
