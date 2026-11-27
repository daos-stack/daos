# Consumer Integrations for GPU Direct I/O (Draft) Design

## Summary

- **Problem**: the GPU direct I/O APIs of [gpuio.md](gpuio.md) are generic. A framework keeps
  its data in GPU memory and reaches storage through layers of its own, and only some of
  those layers hand GPU memory to storage. The others copy to host memory first, and gain
  nothing from these APIs.
- **Scope**: the paths on which a framework can use the GPU direct I/O APIs. Paths that move
  host memory only (a dfuse mount, Dynamo's object store client, LMCache's external
  storage adapters, Mooncake Store) already work with the existing DAOS APIs and are not
  specified here; [Host Memory Paths](#host-memory-paths) lists them in a few lines.
- **Proposal**: DAOS-specific code lives at three integration points, each written once:
  - **A. NIXL backend** (in NIXL): `VRAM_SEG` on the DFS `_ma` calls and
    `daos_mem_register()`, with `FILE_SEG` by file descriptor for Dynamo's disk tier.
  - **B. A cuFile library** (in DAOS): a replacement `libcufile.so`, which serves existing
    cuFile callers. A cuFile plugin and cuObject callbacks are optional; none is verified.
  - **C. A GDS backend in LMCache**: its GPU direct tier on the DFS `_ma` calls.
- **Impact**: no DAOS API, wire or server change here; those are all in
  [gpuio.md](gpuio.md). A and C live in the consumers' repositories. B is new client-side
  code in the DAOS tree, on top of the public APIs.

## Background

### The Stack

```
          Dynamo                  cuFile                   LMCache
            |                  applications                   |
   NIXL (its disk tier)             |             GDS manager (its L1 tier)
      |            |                |                |               |
 [A] DAOS       cuda_gds            |             cuFile         [C] DAOS
 NIXL backend   backend             |             backend        GDS backend
      |            |                |                |               |
      |            +----------------+----------------+               |
      |                             |                                |
      |              [B] DAOS cuFile library (libcufile.so)          |
      |                             |                                |
      +-----------------------------+--------------------------------+
                                    |
           DAOS GPU direct I/O APIs: dfs_{read,write}_ma(),
           daos_obj_{fetch,update}_ma(), daos_mem_register()
```

[A], [B] and [C] are the three integration points, the places with DAOS-specific code.
Dynamo and LMCache are KV cache layers under an inference engine (vLLM, TensorRT-LLM,
SGLang). Dynamo reaches DAOS through [A], or through NIXL's `cuda_gds` backend and [B].
LMCache reaches it through [C], or through its cuFile backend and [B]. Only paths that carry
GPU memory are drawn: LMCache can also reach [A] through the NIXL adapter of its external
storage tier, with host memory only (see [Host Memory Paths](#host-memory-paths)).

cuObject applications are not in the picture: the cuFile library does not serve them, and
their only path is optional (see [cuFile and cuObject](#cufile-and-cuobject)).

The frameworks own block keys, placement across tiers and eviction. This design adds no
KV-cache specific API; a DAOS library with one is proposed separately (see
[gpuio.md](gpuio.md#related-efforts)). NIXL is the transfer library between a framework and
a storage or network backend; cuFile and cuObject are NVIDIA's libraries for I/O between GPU
memory and storage.

### NIXL Segment Types

A NIXL transfer copies data between two descriptor lists. Each list has one segment type,
which says what kind of place the data is in and how a descriptor addresses it:

| Type | The place | A descriptor gives |
|------|-----------|--------------------|
| `DRAM_SEG` | host memory | address, length |
| `VRAM_SEG` | GPU memory | address, length, GPU id |
| `FILE_SEG` | a file | offset, length; the file by descriptor or by path |
| `OBJ_SEG` | an object named by a key | offset, length, key |
| `BLK_SEG` | a block device | offset, length, device id |

The first two are the memory side, where the framework holds a block. The others are the
storage side. A storage transfer pairs one of each, for example a write of a GPU block by key
is `VRAM_SEG` to `OBJ_SEG`.

Each backend declares the types it accepts, and a framework can use a backend for a tier
only if the backend declares the type that tier uses. Backends in the NIXL tree, for
comparison:

| Backend | Memory side | Storage side |
|---------|-------------|--------------|
| `posix` | `DRAM_SEG` | `FILE_SEG` |
| `cuda_gds` (on cuFile) | `DRAM_SEG`, `VRAM_SEG` | `FILE_SEG` |
| `obj` (S3) | `DRAM_SEG`, `VRAM_SEG` | `OBJ_SEG` |

### How Frameworks Hand GPU Memory to Storage

A framework does not call a storage system directly. What decides whether it can use GPU
direct I/O is which of its layers still holds GPU memory when the data goes to storage.

**Dynamo** (from its source, `lib/kvbm-*` and `lib/memory`):

| Tier | Where | Reached through | GPU memory to storage |
|------|-------|-----------------|-----------------------|
| G1 | GPU memory | | |
| G2 | pinned host memory | | |
| G3 | disk: one preallocated file per worker | NIXL, `FILE_SEG` | yes, as `VRAM_SEG` |
| G4 | shared object store | its own object client (S3) | no, host byte slices |

- Dynamo registers device memory with NIXL as `VRAM_SEG`, so GPU blocks do reach a NIXL
  storage backend, but only with `FILE_SEG`.
- It creates the G3 file itself (`mkostemp()` in its disk cache directory, with `O_DIRECT`)
  and passes NIXL the file descriptor.
- It checks backend names: a disk layout needs `POSIX` or `GDS_MT`, and device memory needs
  `UCX` or `GDS_MT`.

**LMCache** (from its source, `lmcache/v1/distributed` and `lmcache/v1/gpu_connector`):

```
 LMCache storage manager
   +-- L1, the local tier             space on this node that LMCache allocates itself
   |     +-- DRAM manager             pinned host memory (the default)
   |     +-- GDS manager              a slab file, written from the GPU; no host buffer
   |           +-- GDS backend        one interface: cuFile, hipFile, ...
   |
   +-- L2, the external storage tier  adapters behind one interface, one of them on NIXL
```

- **GPU direct I/O is in L1, without NIXL.** The GDS manager keeps the tier in a slab file
  and calls a GDS backend: an interface with a slab to open, a GPU buffer to register, and
  reads and writes at an offset.
- **L2 gets host memory only.** An L2 adapter stores what is in the host arena of L1, and
  the NIXL adapters pass that arena as `DRAM_SEG`.
- This is LMCache's current multiprocess mode. Its older in-process mode could give NIXL a
  GPU buffer (`nixl_buffer_device: cuda`, for the `GDS` and `OBJ` backends); that mode was
  moved out of the main repository into a separate legacy package in November 2026.

So a NIXL backend with `VRAM_SEG` is not enough by itself: the framework must also pass GPU
memory to NIXL, as Dynamo does and LMCache does not, and must know the backend by name.

Terms in this document: a NIXL **backend** (or plugin) lives in NIXL; a **GDS backend** and
an **L2 adapter** live in LMCache; "the prototype" in the NIXL sections is the NIXL backend
prototype of [Related Efforts](#related-efforts).

### Related Efforts

Work in progress on the consumer side. Results are quoted as reported, not verified here.
The DAOS side (the DAOS prototype, the external GPU test) is listed in
[gpuio.md](gpuio.md#related-efforts).

- **NIXL backend on libdfs** ([ai-dynamo/nixl#2261](https://github.com/ai-dynamo/nixl/pull/2261),
  open), "the prototype": each key is a file in a DAOS POSIX container. It declares
  `DRAM_SEG` and `OBJ_SEG` only, its `registerMem(DRAM_SEG)` does nothing, and each
  descriptor is one `dfs_read()` or `dfs_write()` with an event. The basis of
  [NIXL Backend](#nixl-backend).
- **NIXL backend on the object API** ([gluesys/nixl-daos](https://github.com/gluesys/nixl-daos),
  not submitted to NIXL): dkeys and akeys, and GPU memory on the DAOS prototype's object
  calls. See [Alternatives Considered](#alternatives-considered).
- **LMCache connector for DAOS**
  ([gluesys/lmcache-daos](https://github.com/gluesys/lmcache-daos)): an out-of-tree plugin on
  libdfs. Its supported modes move host memory, among them an L2 adapter for LMCache's
  current multiprocess mode. Its GPU mode, used by the external test, is experimental: a
  storage plugin for LMCache's older in-process mode on the DAOS prototype's DFS calls, so
  it is not a GDS backend in the sense of [LMCache GDS Backend](#lmcache-gds-backend).
- **cuFile and cuObject**: the DAOS prototype has a cuFile plugin and a design for cuObject
  callbacks. Its key import cannot work (see
  [gpuio.md](gpuio.md#a7-import-an-rdma-key-registered-by-another-library)); the plugin
  without it is an optional route; the chosen one is a replacement `libcufile.so`.

## Goals and Non-Goals

### Goals

- Every framework that can hand GPU memory to storage has a stated path to the DAOS GPU
  direct I/O APIs, or a stated reason why not yet.
- GPU memory to DAOS without a staging copy in the framework.
- DAOS-specific code in as few places as possible: one integration point serves several
  frameworks.

### Non-Goals

- Changes to the DAOS APIs, wire protocol or server: they are all in [gpuio.md](gpuio.md).
  The cuFile library is a new client library on top of the public APIs.
- Paths that move host memory only ([Host Memory Paths](#host-memory-paths)).
- KV-cache semantics and eviction policy.
- Using NVIDIA's RDMA descriptor on the DAOS server: a server and wire change, see the end
  of [cuFile and cuObject](#cufile-and-cuobject).

## Design

### Integration Points

| | Integration point | Lives in | DAOS APIs | Serves | Status |
|-|-------------------|----------|-----------|--------|--------|
| A | NIXL backend | NIXL | DFS `_ma` | frameworks that give NIXL GPU memory | host only |
| B | cuFile library | DAOS | DFS `_ma` | cuFile callers | unverified |
| C | GDS backend | LMCache | DFS `_ma` | LMCache | nothing exists |

All three use `daos_mem_register()` for the GPU buffer, where their interface has a
registration call.

### Frameworks

| Framework | Its GPU direct tier | Integration point | Needs in the framework |
|-----------|---------------------|-------------------|------------------------|
| Dynamo | G3 through NIXL | A with `FILE_SEG` | DAOS in its name checks; cache on dfuse (*) |
| Dynamo | G3 through NIXL | B, under `GDS_MT` | nothing; cache directory on dfuse (*) |
| LMCache | L1, GDS manager | C | one new backend file |
| LMCache | L1, GDS manager | B, the replacement library | nothing |
| LMCache | L2 through NIXL | A | an L1 arena in GPU memory, passed as `VRAM_SEG` |
| cuFile applications | the library | B | nothing |
| cuObject applications | the library | none scheduled | optional callbacks |

(*) Dynamo creates the file with `fallocate()`, which dfuse lacks; see
[Future Works](#future-works).

Both Dynamo's G3 and LMCache's L1 are per-node tiers: DAOS then holds a slab file per worker,
addressed by offset, and sharing between nodes still goes through a host memory path. No
framework read for this document has a GPU direct path to a shared tier today.

### NIXL Backend

Integration point A. The prototype stores each key as a DFS file and moves host memory. For
GPU direct I/O it needs `VRAM_SEG`, and `FILE_SEG`, the storage type of the one framework
that gives NIXL GPU memory today. NIXL registers a memory pool once and passes no RDMA key,
which is what `daos_mem_register()` and the DFS `_ma` calls serve.

#### Storage Types

Both storage types end in a DFS file and the same `dfs_{read,write}_ma()` calls; they differ
only in how the file is found.

| Storage type | How the backend finds the file | GPU consumer today |
|--------------|--------------------------------|--------------------|
| `FILE_SEG` | the caller's file descriptor, of a file on a dfuse mount | Dynamo G3 |
| `OBJ_SEG` | a file named by the key | none |

- With `FILE_SEG` the framework opens the file itself and passes the descriptor, so the file
  has to be on a dfuse mount: for Dynamo, its disk cache directory. The backend maps the
  descriptor to the DFS file (see the
  [appendix](#appendix-resolving-a-dfuse-file-descriptor)), and the I/O goes through
  `dfs_*`, not through FUSE.
- `OBJ_SEG` with `VRAM_SEG` works with the same code, but no framework passes that pair
  today.

#### Backend Mapping

| NIXL | DAOS |
|------|------|
| `registerMem(VRAM_SEG)` | `daos_mem_register()` of the pool, then `daos_mem_bind()` to the lane |
| `deregisterMem(VRAM_SEG)` | `daos_mem_deregister()` after the transfers complete |
| `registerMem(FILE_SEG)` | the descriptor mapped to a DFS file; released at `deregisterMem` |
| `OBJ_SEG` descriptor | a file named by the key |
| `VRAM_SEG` descriptor | `dfs_{read,write}_ma()`: `DAOS_MEM_TYPE_CUDA`, device id, `ma_handle` |
| transfer with many descriptors | one DFS call per descriptor, with events |

Pool, container and object class come from the init parameters.

#### Rules for GPU I/O

1. **No holes.** A device memory read over a hole fails, since DAOS cannot zero GPU memory.
   A slab (`FILE_SEG`) has to be written before it is read, or the framework must only read
   ranges it wrote. A block per key (`OBJ_SEG`) is written whole at offset 0.
2. **Short reads.** Compare the read size with the descriptor length; a read past the end
   leaves the rest of the GPU buffer untouched and is a miss.
3. **Object class and checksums.** GPU I/O is rejected on EC objects and on updates to
   checksum or dedup containers. DFS files are EC by default in a redundant container, and
   `dfs_suggest_oclass()` may pick EC. Create files with an explicit replicated or
   non-redundant class, and the container with checksums off (`cksum:off`; they are on by
   default) and without dedup. Replicated classes are still to be proven on hardware (see
   the risks of [gpuio.md](gpuio.md)). Server-side EC ([server_ec.md](server_ec.md)) lifts
   the EC restriction for arrays.
4. **A rejected GPU I/O is an error, not a copy.** The backend does not link with CUDA and
   does not stage data. It checks what it can when the file is opened (the object class),
   and the framework falls back to host memory.
5. **Complete blocks, for `OBJ_SEG`.** A reader must never get a partly written block. The
   prototype writes in place, so the backend has to publish a block by rename, or keep a
   block within one chunk. This is a correctness rule for host memory as well, and belongs
   to the review of the prototype, not to GPU support.
6. **Open handles.** Keep files open across transfers; `dfs_lookup()` walks the path with
   one RPC per component and DFS has no lookup cache.

#### Event Queue Lanes

A lane is one DAOS event queue plus the thread that polls it (`num_event_queues`, default 1;
`max_inflight_per_queue`, default 1024). In DAOS each event queue is a CaRT context with its own
network domain, so every lane pins a registered region once more.

**Restriction**: with `VRAM_SEG`, use one lane per process, and scale with one process per GPU,
the usual layout of vLLM and Dynamo workers.

- With N lanes the pool is pinned N times; a lane that hits a NIC or BAR1 limit falls back to
  per-I/O registration (slower, not wrong).
- Set `D_INTERFACE` and `D_DOMAIN` per process to the NIC next to its GPU (`nvidia-smi topo -m`).
- One lane is not measured; the lane scaling benchmark of
  [gpuio.md](gpuio.md#testing--validation) decides it. The eventual fix is a
  network domain shared by all contexts
  ([gpuio.md](gpuio.md#future-works)).

#### GPU Memory Support

What the prototype has to change for `VRAM_SEG`:

- Add `VRAM_SEG` to `getSupportedMems()` and to the transfer validation.
- `registerMem(VRAM_SEG)` calls `daos_mem_register()` with `DAOS_MEM_TYPE_CUDA` and the device
  id, keeps the handle in the backend metadata, and calls `daos_mem_bind()` on the network
  domain of its event queue (`daos_eq_net_domain()`), so that the first transfer does not pay
  for the registration and a pool too large to register is reported then; `deregisterMem` calls
  `daos_mem_deregister()` and must handle `-DER_BUSY`. If
  `daos_mem_register()` returns `-DER_NOSYS` (the MR cache is off), the backend keeps no handle
  and its I/O registers per I/O; `-DER_NOTSUPPORTED` means GPU memory cannot be used and
  `registerMem` fails. If `daos_mem_bind()` fails, the pool cannot be registered as one region
  (for example it is too large): the backend can register smaller regions, or go on with
  per-I/O registration.
- Each I/O carries a `daos_mem_attr_t` next to its iov and calls `dfs_{read,write}_ma()`.
- The threads that call `registerMem`, post transfers and poll the event queue need a current
  CUDA context of the device (see [gpuio.md](gpuio.md#public-apis)).
- A transfer that the backend gives up on (timeout) is still running in DAOS, since
  `daos_event_abort()` does not cancel it. The backend must report the block as in use until
  the event completes, and `deregisterMem` returns busy until then.
- Detect the `_ma` calls at build time, so the backend still builds against a DAOS without
  them.
- Keep to one lane when GPU memory is registered (see
  [Event Queue Lanes](#event-queue-lanes)).

#### Expected Benefit

- No staging copy in the framework. Expect a host memory bandwidth gain, not a throughput
  gain; see the external measurement in [gpuio.md](gpuio.md#testing--validation).
- The external test concludes that this gain is not worth much on a host with one GPU,
  where host memory bandwidth is not the limit, and that it matters with several GPUs per
  host. It did not measure that case, so the benefit is still to be shown where it counts.
- With a slab (`FILE_SEG`) there is no create or lookup per block, so the metadata cost per
  block that the DFS file per key layout pays does not apply.

### cuFile and cuObject

Integration point B. No route is verified yet. The replacement library is the chosen
route; the reasons are with the two routes below. It matters because one verified route
would serve everything built on cuFile at once: NIXL's `cuda_gds` backend (and through it
Dynamo's disk tier), LMCache's GDS manager, and plain cuFile applications such as model
loaders and GPU data frames.

Both libraries register the GPU buffer themselves and pass the storage system an RDMA
descriptor, which DAOS cannot use on its own connections (A.7 in
[gpuio.md](gpuio.md#a7-import-an-rdma-key-registered-by-another-library)). A route therefore
has to hand the GPU buffer itself to DAOS, which registers it on its own connection and
ignores the descriptor. The routes below all do that, so none of them needs a server or
wire change.

**cuFile** has two routes on the current APIs. Neither has been run on a GPU:

1. **A DAOS-provided `libcufile.so`** (chosen) that implements the cuFile calls for DAOS files and
   forwards other files to NVIDIA's library. `cuFileBufRegister()` maps to
   `daos_mem_register()` and `daos_mem_bind()`, `cuFileRead()` and `cuFileWrite()` to
   `dfs_{read,write}_ma()` with the device address the application passed, so there is one
   registration, held as a region. The cost is that it replaces NVIDIA's library: it has to
   follow the `cufile.h` ABI and be loaded in its place.
2. **A cuFile userspace plugin** (optional) (`CUfileFSOps_t`, file handle type
   `CU_FILE_HANDLE_TYPE_USERSPACE_FS`). The application keeps NVIDIA's `libcufile.so` and
   registers the DAOS callbacks for its file. The prototype implements this
   (`src/client/cufile/` on its branch), and it ports to this design by calling
   `dfs_{read,write}_ma()` in the callbacks; the only feature lost is the import of the
   cuFile key, so DAOS registers the buffer itself.
   - The interface has no buffer registration hook, so the plugin cannot hold a
     `daos_mem_register()` region safely, and every I/O registers its buffer. With the
     transport registration cache on (see [gpuio.md](gpuio.md#memory-registration)) a buffer
     that is reused is registered once, on its first I/O; an application that does I/O on
     many different buffers can still fill the cache. Either way the memory is pinned by
     cuFile and by DAOS.
   - **Open question**: what the callback buffer is when cuFile
     passes an RDMA descriptor. The prototype passes it to DAOS as a CUDA device address,
     while its own document describes it as always host-accessible memory (a bounce buffer
     or a BAR1 mapping of the GPU buffer). A BAR1 mapping is not CUDA memory for the
     transport. The prototype has no test of this path.
   - Without a descriptor cuFile stages the data in a host buffer and the plugin uses the
     host calls. That works today and is not GPU direct I/O.

Why the library is chosen: it holds one registration as a region, it does not depend on
the open question of the plugin, and it serves more callers. The plugin only serves an
application that registers its file with the plugin's handle type and callbacks. A caller
that registers a plain file descriptor, as NIXL's `cuda_gds` backend and LMCache's cuFile
backend do, is served only by the replacement library, unless it is changed.

Both routes need the following, in the plugin or the shim, which may link with CUDA:

- A mapping from the file descriptor of a dfuse file to the DFS file; see the
  [appendix](#appendix-resolving-a-dfuse-file-descriptor).
- A host fallback where a GPU I/O is rejected: a hole, an EC file, an update on a container
  with checksums (see [gpuio.md](gpuio.md#errors-and-fallback)).
- The device of the buffer, for `ma_device_id`; the prototype always passes device 0.

**cuObject** is not served by the replacement library: its applications use NVIDIA's
cuObject client library, not `libcufile.so`. It has one candidate route, optional and not
scheduled, the same idea as the cuFile plugin. The application
gives `cuObjClient` a get and a put callback, which receive a buffer, a size, an offset and
the RDMA descriptor. The callbacks ignore the descriptor and call
`daos_obj_{fetch,update}_ma()` on the buffer; the prototype has a design for this and no
code. It shares the limits of the cuFile plugin: no registration hook, so a registration per
I/O unless the registration cache is on, a second pin, and the same open question about
what the callback buffer is. It also
needs a mapping from the cuObject key to a DAOS object, dkey and akey.

**Not a route here: using the descriptor.** The model NVIDIA documents has the storage
server start the RDMA from the descriptor, with NVIDIA's server library, over DC. In DAOS
that means the descriptor travels in the RPC and the server does the RDMA into or out of
its I/O buffer in place of the bulk transfer: a server and wire change, for NVIDIA NICs
only. It would remove the second registration, and it is a Non-Goal of
[gpuio.md](gpuio.md#non-goals), so choosing it means a new design.

### LMCache GDS Backend

Integration point C. LMCache's GDS manager calls a GDS backend, and cuFile is one
implementation of it. A DAOS implementation maps one to one onto [gpuio.md](gpuio.md),
without cuFile or a dfuse mount:

| GDS backend call | DAOS |
|------------------|------|
| open the slab | `dfs_connect()`, `dfs_open()` |
| register a GPU buffer | `daos_mem_register()`, `daos_mem_bind()` |
| read or write at an offset | `dfs_{read,write}_ma()` with an event |
| deregister the buffer | `daos_mem_deregister()` |

- The interface has a registration call, so a registered region is held, which the cuFile
  plugin interface cannot do.
- The slab is one large preallocated file addressed by offset: no create or lookup per
  block, and no holes once it is written.
- The limits of [gpuio.md](gpuio.md#client-side-safety-checks) apply to the slab file: a
  replicated or non-redundant class, and a container with checksums off.
- The backend has to be accepted into LMCache, where backends are found by file name inside
  its package.
- The alternative with no LMCache change is its existing cuFile backend on the replacement
  library of [cuFile and cuObject](#cufile-and-cuobject). With NVIDIA's library a slab on a
  dfuse mount works only in cuFile's compatibility mode, through a host buffer.

LMCache's plans may change this picture. Its roadmap for 2026 Q4 lists support for
out-of-tree GDS backends, which would let a DAOS GDS backend live outside LMCache, and GDS
adapters for the external storage tier, which would be its first GPU direct path to a shared
tier. Whether that path would use the GDS backend interface or NIXL is not stated; a DAOS
GDS backend covers the first case, and `VRAM_SEG` in the NIXL backend the second.

This is read from LMCache's source and issue tracker, not tried.

### Host Memory Paths

Out of scope, listed so that the picture is complete. They need no GPU direct I/O API
today:

- **dfuse mount**: any framework's file tier, with NIXL's `posix` backend or without NIXL.
- **Dynamo G4**: its object client speaks S3 only, so DAOS serves it through an S3 gateway
  (`libds3` is the DAOS library for gateways such as RGW), with no DAOS code in Dynamo. Its
  lock protocol needs conditional PUT (`If-None-Match`) from the gateway; not tried.
- **LMCache L2**: its NIXL store adapter on the DAOS NIXL backend (DAOS has to be added to
  LMCache's backend name lists), or a DAOS L2 adapter loaded as a plugin; one exists
  out of tree ([gluesys/lmcache-daos](https://github.com/gluesys/lmcache-daos)).
- **A DAOS KV-cache library**
  ([daos-stack/daos#18775](https://github.com/daos-stack/daos/pull/18775), a design draft):
  a store that a framework reaches through an adapter. Its committed path is host memory;
  with GPU memory it would call `daos_obj_{fetch,update}_ma()`.
- **Mooncake Store**: it copies device memory to a pinned host pool before any storage
  backend sees it, so a DAOS backend for it moves host memory. If Mooncake gets a device
  memory path, such a backend can use `daos_obj_{fetch,update}_ma()`.

## Implementation Phases

After phase 3 of [gpuio.md](gpuio.md#implementation-phases). Phases 1 and 2 are in the
consumers' repositories; phases 3 and 4 are new client-side code in the DAOS tree:

1. **NIXL backend**: `VRAM_SEG` on the DFS `_ma` calls, with the one-lane restriction, and
   `FILE_SEG` by file descriptor. It comes first because the backend exists upstream and
   Dynamo already passes GPU memory.
2. **LMCache GDS backend**: a small backend on an interface that has a registration call.
3. **Replacement `libcufile.so`**: the basic calls first (driver, file handle, buffer,
   synchronous read and write), then the batch calls that NIXL's `cuda_gds` backend uses
   and the stream calls that LMCache's cuFile backend uses. It serves existing cuFile
   callers without a change on their side.
4. **cuFile plugin and cuObject callbacks**, only if an application wants them: they need a
   test on a GPU node of what the callback buffer is, and serve only applications that
   register with them.
5. **EC files**: after server-side EC for arrays ([server_ec.md](server_ec.md), phase 1).

Each phase starts with a run on a GPU node; the hardware checks of
[gpuio.md](gpuio.md#testing--validation) come before all of them.

## Compatibility & On-Disk Impact

None in DAOS. The files are ordinary DFS files that any DFS client can read. See
[gpuio.md](gpuio.md#compatibility--on-disk-impact).

## External Interfaces

None in DAOS. The integrations use the APIs listed in
[gpuio.md](gpuio.md#external-interfaces). The parameters of the NIXL backend are those of the
prototype.

## Testing & Validation

On a GPU node; the NIXL and LMCache tests are in the consumers' repositories, the cuFile
tests in the DAOS tree:

- NIXL backend: a NIXL transfer test with `VRAM_SEG` against a DAOS container, with
  `FILE_SEG` and with `OBJ_SEG`, on a non-redundant and on a replicated object class, on
  OFI/verbs and UCX; a read over a hole and a short read; a file of an EC class is rejected.
- A Dynamo run whose disk tier is the DAOS NIXL backend, and an LMCache run whose GDS tier
  is a DAOS slab, that offload and reload KV blocks.
- cuFile: a cuFile read and write of a DAOS file from GPU memory through the replacement
  library, then NIXL's `cuda_gds` backend and LMCache's cuFile backend on it. For the
  plugin, first the test of what its callback buffer is.
- Throughput and host memory traffic against host staging, for each integration point.

## Risks, Mitigations and Future Works

### Risks and Mitigations

- **Nothing is verified.** All three integration points are read from source. Mitigation:
  the phases start with the NIXL backend, the only one with existing code and a framework
  that already passes it GPU memory.
- **The replacement `libcufile.so` has to follow NVIDIA's API**, about 20 functions today,
  and be loaded in place of NVIDIA's library. Mitigation: forward what it does not
  implement, and every file that is not on DAOS.
- **Upstream dependencies**: `VRAM_SEG` and `FILE_SEG` have to be accepted on the NIXL PR,
  Dynamo has to accept a DAOS backend name, and LMCache a DAOS GDS backend.
- **Local tiers only**: the GPU direct tiers of Dynamo and LMCache are per node. DAOS as a
  shared store adds little there until a framework has a GPU direct shared tier.
- **No data integrity check** without client checksums, and no EC until phase 4 of
  [gpuio.md](gpuio.md#implementation-phases); deployers
  must accept this for these containers.
- **Space reclaim** for `OBJ_SEG`: the NIXL backend interface has no delete call, so the
  framework has to remove files itself.

### Future Works

- `FILE_SEG` by path (NIXL's path mode, where the backend opens the file with `dfs_open()`),
  if a framework passes GPU memory with a path. It avoids the dfuse mount, as long as the
  framework does not also manage the files itself.
- `fallocate` in dfuse. Dynamo preallocates its disk tier file with `fallocate()`, which
  dfuse does not implement, so that tier is expected to fail to start on a dfuse mount
  (from source, not tried). Mode 0 can map to setting the file size (`dfs_punch()`). The
  other way is a fallback to `ftruncate()` in Dynamo, which it has for other platforms.
  Either is needed before `FILE_SEG` serves Dynamo. LMCache's slab uses
  `posix_fallocate()`, which the C library emulates, and a DAOS GDS backend opens the slab
  with `dfs_open()`, so neither depends on it.
- `OBJ_SEG` with `VRAM_SEG` for a shared tier, once a framework passes GPU memory with keys.
- A Mooncake backend on the object `_ma` calls, once Mooncake has a device memory path.

## Alternatives Considered

- **The DAOS object API under the NIXL backend, one value per block.** A single value is
  atomic and needs no create, lookup or rename, and an external backend
  ([gluesys/nixl-daos](https://github.com/gluesys/nixl-daos)) reports a much lower fixed
  cost per small block this way (its figures, not verified). Not chosen:
  - a block in one dkey goes to one target as one transfer, and the server needs DMA buffer
    space for the whole value at once on that target (8 MiB chunks, 256 MiB to 1 GiB per
    target). Blocks of tens of MiB then queue under load, and larger ones fail; a DFS file
    spreads the block over targets in 1 MiB pieces;
  - splitting a block over dkeys is not atomic either, and the fix on host memory, a
    transaction, is rejected for GPU updates;
  - the data is not visible to other DFS clients, and two backends with different layouts
    cannot read each other's data.

  Worth taking from that backend: its GPU memory experience (about 0.43 ms per sgl entry for
  a registration per I/O, the cost `daos_mem_register()` removes), a deadline when polling,
  and several descriptors in one request. Its borrowed event queues conflict with a
  registered region, which is pinned once per event queue.
- **A DAOS L1 manager in LMCache** instead of a GDS backend: more code for the same result,
  since the GDS manager already does the slab allocation.
- **GPU direct through LMCache's L2** (a GPU arena passed to NIXL as `VRAM_SEG`): the only
  way to a GPU direct shared tier in LMCache, but it is a change of LMCache's design, not an
  integration.

## Appendix: Resolving a dfuse File Descriptor

A cuFile route, and `FILE_SEG` in the NIXL backend, start from a file that the application
opened through a dfuse mount, and have to do their I/O through libdfs, which needs a `dfs_t`
and a `dfs_obj_t`. dfuse hands them out through ioctls on the file descriptor
(`src/include/dfuse_ioctl.h`). The interception libraries (`libioil`, `libpil4dfs`) use them
in the same way, and the prototype's plugin does it in `resolve_fd_to_handle()`:

| Step | ioctl on the descriptor | Then |
|------|-------------------------|------|
| 1 | `DFUSE_IOCTL_IL_SIZE` | sizes of the pool, container and DFS handles; check the version |
| 2 | `DFUSE_IOCTL_REPLY_POH` | `daos_pool_global2local()` |
| 3 | `DFUSE_IOCTL_REPLY_COH` | `daos_cont_global2local()` |
| 4 | `DFUSE_IOCTL_REPLY_DOH` | `dfs_global2local()` gives the `dfs_t` |
| 5 | `DFUSE_IOCTL_IL_DSIZE` | size of the file handle; check the version |
| 6 | `DFUSE_IOCTL_REPLY_DOOH` | `dfs_obj_global2local()` gives the `dfs_obj_t` |

- The ioctl fails with `ENOTTY` on a file that is not on dfuse, which is how a shim tells a
  DAOS file from one it must forward. A version other than `DFUSE_IOCTL_VERSION` means dfuse
  and the library do not match, and the file is not taken over.
- The handles are those of the dfuse process, so no pool or container is connected again and
  the file is not opened by path. The process needs `daos_init()` and an agent.
- I/O through the `dfs_obj_t` bypasses the kernel and dfuse, so the dfuse data cache does not
  see it. `libioil` has the same property.
- The result is cached per descriptor. The entry has to be dropped when the descriptor is
  closed, since the number is reused. The plugin interface has no close callback, so a
  plugin has to give the application a call for it; a shim sees `cuFileHandleDeregister()`
  itself.

What the prototype's version lacks, and a port has to add:

- It imports a pool, a container and a DFS handle for every descriptor. `DFUSE_IOCTL_IL`
  returns the pool and container UUIDs, which lets files of one container share them, as
  `libioil` does.
- It never drops a cached entry, so a reused descriptor number gets the previous file.
- It tells a descriptor from a handle pointer by the value (below 65536 is a descriptor).

## References

- [gpuio.md](gpuio.md), [server_ec.md](server_ec.md)
- [NIXL DAOS backend prototype](https://github.com/ai-dynamo/nixl/pull/2261)
- [NIXL plugin on the DAOS object API](https://github.com/gluesys/nixl-daos) and the external
  test, [gluesys/lmcache-daos](https://github.com/gluesys/lmcache-daos)
- [NVIDIA Dynamo](https://github.com/ai-dynamo/dynamo)
- [LMCache](https://github.com/LMCache/LMCache)
- [NVIDIA GDS overview](https://docs.nvidia.com/gpudirect-storage/overview-guide/) (cuFile),
  [NVIDIA cuObject](https://docs.nvidia.com/gpudirect-storage/cuobject/index.html)
