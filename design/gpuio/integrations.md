# Consumer Integrations for GPU Direct I/O (Draft) Design

## Summary

- **Problem**: the GPU direct I/O APIs of [gpuio.md](gpuio.md) are generic. Each consumer
  (NIXL with LMCache and Dynamo, Mooncake Store, NVIDIA cuFile and cuObject) reaches DAOS
  through a plugin, backend or connector of its own, and each has its own way to name data
  and to own GPU buffers.
- **Proposal**: one document for all of them: how each consumer maps onto the DAOS APIs, what
  its plugin must do to be correct, and what is missing.
  1. **NIXL backend** (in the NIXL repository): each key is a DFS file; GPU memory moves with
     the DFS `_ma` APIs and `daos_mem_register()`. It serves LMCache, and Dynamo once Dynamo
     stores blocks through NIXL by key.
  2. **Mooncake Store backend**: one record per value through the object APIs.
  3. **cuFile**: two candidate routes on the current APIs (a plugin and a replacement
     library), and **cuObject**: one (its callbacks). No server change; none verified.
- **Impact**: nothing is added to DAOS here; the DAOS changes are all in
  [gpuio.md](gpuio.md). The integrations live in the consumers' repositories.

## Background

### The Stack

```
 Inference engine (vLLM, TensorRT-LLM, SGLang)
          |
 Dynamo (KV block manager)   LMCache (cache engine)   Mooncake Store   cuFile / cuObject apps
          +-----------+-----------+                         |                    |
                      |                                     |                    |
        NIXL agent API -> storage backend plugin     storage backend      libcufile plugin
                      |                                     |                    |
        dfs_{read,write}_ma()                    daos_obj_{fetch,update}_ma()   (not verified)
                      +------------------+------------------+
                                         |
                         daos_mem_register(), CaRT bulk with memory attributes
```

The frameworks own block keys, placement across tiers and eviction. DAOS gets no KV-cache
specific API. NIXL is the transfer library between a framework and a storage or network
backend; Mooncake and the cuFile and cuObject libraries have interfaces of their own.

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

### Where DAOS Fits

DAOS is the storage client library and the servers under one backend. The two sides of a
transfer are two independent choices in that backend:

| Side | Type | In DAOS |
|------|------|---------|
| Memory | `DRAM_SEG` | the existing calls (`dfs_read()`, `daos_obj_fetch()`, ...) |
| Memory | `VRAM_SEG` | the `_ma` calls and `daos_mem_register()` of [gpuio.md](gpuio.md) |
| Storage | `OBJ_SEG` | a DFS file (or a DAOS object) that the backend opens by key |
| Storage | `FILE_SEG` | a DFS file, read and written with the `dfs_*` calls |

- **The memory side is what [gpuio.md](gpuio.md) adds.** Without `VRAM_SEG` in the backend,
  GPU data still reaches DAOS, but the framework copies it to host memory first.
- **The storage side is a choice of layout, not of DAOS API.** `OBJ_SEG` only means that data
  is addressed by a key; a DFS file per key is an `OBJ_SEG` backend. Both storage types end
  in the same DFS file and the same `dfs_*` read and write calls, host or `_ma`. They differ
  only in how the file is found.
- **`FILE_SEG` needs no dfuse mount.** NIXL gives a file to a backend in one of two ways:
  - *Path mode*: the path is in the descriptor's metadata, and the backend opens and closes
    the file. A DAOS backend opens it with `dfs_open()` or `dfs_lookup()` in its container.
    This is the mode to implement first.
  - *Descriptor mode*: the caller opened the file itself and passes the file descriptor.
    Only then must the file be on a dfuse mount, and the backend maps the descriptor to the
    DFS file (see the [appendix](#appendix-resolving-a-dfuse-file-descriptor)); the I/O
    still goes through `dfs_*`, not through FUSE.
- **One storage type makes a working backend, but the framework chooses per tier.** This
  design starts with `OBJ_SEG` ([NIXL Backend](#nixl-backend)); `FILE_SEG` is
  [Future Works](#future-works). In a backend on libdfs it is a small addition, since only
  the lookup of the file is new.
- **DAOS can also sit under another backend, with no DAOS backend at all**: under `posix`
  through a dfuse mount (host memory, at the cost of FUSE), and under `cuda_gds` through a
  cuFile route, once one is verified ([cuFile and cuObject](#cufile-and-cuobject)), which
  gives `FILE_SEG` with `VRAM_SEG`.
- **Declaring a type is not enough.** A framework selects a backend by name, so it also has
  to know the DAOS backend ([LMCache](#lmcache), [Dynamo](#dynamo)).

### The NIXL Backend Prototype

The NIXL backend prototype
([ai-dynamo/nixl#2261](https://github.com/ai-dynamo/nixl/pull/2261)), called "the prototype"
in the NIXL sections of this document, stores each NIXL key as a file in a DAOS POSIX
container. It supports `DRAM_SEG` and `OBJ_SEG` only, so the framework has
to copy GPU data to host memory first. Its `registerMem(DRAM_SEG)` does nothing, and each
descriptor is one `dfs_read()` or `dfs_write()` with one iov and an event.

### Related Efforts

Work in progress on the consumer side. Results are quoted as reported, not verified here.
The DAOS side (the DAOS prototype, its review, the external GPU test) is listed in
[gpuio.md](gpuio.md#related-efforts).

- **NIXL backend on libdfs** ([ai-dynamo/nixl#2261](https://github.com/ai-dynamo/nixl/pull/2261),
  open): host memory, `OBJ_SEG`. The basis of [NIXL Backend](#nixl-backend).
- **NIXL backend on the object API** ([gluesys/nixl-daos](https://github.com/gluesys/nixl-daos),
  prepared for NIXL upstream, not submitted): dkeys and akeys, `FILE_SEG`, GPU memory on the
  DAOS prototype's object calls. Compared in the
  [appendix](#appendix-the-two-nixl-backends-for-daos). Its repository also carries the
  LMCache patch that adds DAOS to the backend name lists, and a vLLM run through it.
- **LMCache backends for DAOS** ([gluesys/lmcache-daos](https://github.com/gluesys/lmcache-daos)):
  a native DFS connector for host memory and the GPU backend used by the external test. See
  [LMCache](#lmcache).
- **Dynamo** stores its shared tier through its own S3 client; NIXL object storage is not
  enabled in its source. See [Dynamo](#dynamo).
- **Mooncake** ([kvcache-ai/Mooncake](https://github.com/kvcache-ai/Mooncake)): its storage
  layer takes host memory only and copies device memory first; an RFC for a distributed KV
  storage backend ([#2993](https://github.com/kvcache-ai/Mooncake/issues/2993)) is closed, and
  a generic distributed backend with file and object adapters is in its tree. No DAOS backend
  or adapter exists yet. See [Mooncake Store](#mooncake-store).
- **cuFile and cuObject**: the DAOS prototype has a cuFile plugin and a design for cuObject
  callbacks. Its key import cannot work (see
  [gpuio.md](gpuio.md#a7-import-an-rdma-key-registered-by-another-library)); the plugin
  without it is a candidate route, see [cuFile and cuObject](#cufile-and-cuobject).

## Goals and Non-Goals

### Goals

- Every consumer of the requirement has a stated path to DAOS, or a stated reason why not yet.
- GPU memory through NIXL to DAOS without a staging copy.
- Blocks that are atomic and complete, for host memory too.
- One NIXL backend for all frameworks that store blocks through NIXL by key (`OBJ_SEG`).

### Non-Goals

- Changes to DAOS: they are all in [gpuio.md](gpuio.md).
- KV-cache semantics and eviction policy.
- Moving the host memory path to registered regions, unless a measurement shows a gain.
- A chosen route for cuFile and cuObject; the routes are only compared here.

## Design

### Consumers

| Consumer | DAOS path | Memory | Phase of [gpuio.md](gpuio.md) | Status |
|----------|-----------|--------|-------------------------------|--------|
| NIXL | backend, files over DFS `_ma` | host, GPU | 3 | backend upstream, host only today |
| LMCache | NIXL backend (or native DFS connector) | host, GPU | 3 | needs DAOS in its name lists |
| Dynamo | disk tier: dfuse; object tier: NIXL backend | host; GPU by key | 3 | [Dynamo](#dynamo) |
| Mooncake | object API, one record per value; or dfuse | host | 2 | no device path in Mooncake |
| cuFile | shim or plugin over DFS `_ma` | GPU | future | two candidate routes, not verified |
| cuObject | callbacks over object `_ma` | GPU | future | one candidate route, not verified |

### NIXL Backend

The backend stores each key as a DFS file and declares `OBJ_SEG`. NIXL registers a memory pool
once and passes no RDMA key, which is what `daos_mem_register()` and the DFS `_ma` calls serve.

#### Backend Mapping

| NIXL | DAOS |
|------|------|
| `registerMem(VRAM_SEG)` | `daos_mem_register()` of the pool, then `daos_mem_bind()` to the lane |
| `deregisterMem(VRAM_SEG)` | `daos_mem_deregister()` after the transfers complete |
| `registerMem(DRAM_SEG)` | nothing, as in the prototype |
| `OBJ_SEG` descriptor (key in `metaInfo` or `devId`, offset in `addr`) | file named by the key |
| `queryMem` (existence) | `dfs_lookup_rel()`; a published file is complete (rule 1) |
| transfer with many descriptors | one DFS call per descriptor, with events |
| `VRAM_SEG` descriptor | `dfs_{read,write}_ma()`: `DAOS_MEM_TYPE_CUDA`, device id, `ma_handle` |
| `DRAM_SEG` descriptor | the plain `dfs_read()` and `dfs_write()` |

Host memory is not registered (see [Expected Benefit](#expected-benefit)). Pool, container and
object class come from the init parameters.

#### Backend Rules

A DFS file has byte array semantics: an overwrite keeps the old size, a write larger than a
chunk is one update per chunk, and a file exists as soon as it is created. A block is atomic
and complete only if the backend follows these rules. Rules 1 and 2 fix correctness bugs of the
prototype that exist for host memory too.

1. **Publish a block by rename**: write the block to a temporary name in the same directory
   (the key plus a unique suffix), and `dfs_move()` it over the key when the write completes.
   A reader then finds the old block, the complete new one, or no block, never a partial
   one. This removes three failures of the prototype at once:
   - a stale tail, when a smaller block overwrites a larger one without `O_TRUNC` and a reader
     with a larger buffer gets the old tail;
   - a torn block, when a writer stops between the per-chunk updates;
   - an empty key reported present, because the prototype creates the file in the write
     `prepXfer`, before any data is written, and forever if the writer dies.

   `dfs_move()` over an existing file is one transaction only on a balanced-mode mount
   (`DFS_BALANCED`, or `DFS_USE_DTX=1`). On the default relaxed mount it is three steps: punch
   the old file and its entry, insert the new entry, punch the temporary entry. So:
   - a reader between the first two steps gets `ENOENT`, a miss, which a cache accepts;
   - a writer that dies there loses the old block and leaves its temporary file;
   - of two writers publishing one key, one can get `EEXIST` or `ENOENT` from `dfs_move()`. It
     removes its temporary file and counts the block as published by the other writer.

   The backend uses the relaxed mount; a balanced mount would cost a transaction per rename.

   The rule costs one rename per block and keeps the chunk size small, so a large block is
   still written to several targets in parallel. A writer that dies leaves a temporary file,
   which the space reclaim has to remove (see [Risks](#risks-mitigations-and-future-works)).
2. **Small blocks**: where the rename costs too much, a block no larger than `chunk_size`,
   written at offset 0, is a single update and needs no rename. The backend must then truncate
   before an overwrite (`O_TRUNC` or `dfs_punch()`), unless all blocks have one size, and report
   a key present only if its size is non-zero. The prototype's `chunk_size` defaults to the DFS
   default, 1 MiB, and nothing checks it against the transfer length.
3. **Short reads**: compare the read size with the descriptor length (the prototype returns
   `ENODATA`), so a read past the end is a miss. A block written whole at offset 0 has no
   holes, which device memory reads cannot handle.
4. **Metadata cost**: the prototype does `dfs_lookup()` and `dfs_open()` per descriptor in every
   `prepXfer`. Cache open handles, with keys in one flat directory and `dfs_lookup_rel()`.
   A cached handle goes stale when its key is published again: the rename of rule 1 punches
   the old object, so a read through the old handle is short (size 0), never old or mixed
   data. On a short read, drop the handle, look up the key again and retry once before
   reporting a miss.
5. **GPU I/O** needs `dfs_{read,write}_ma()` with events (see
   [GPU Memory Support](#gpu-memory-support)).
6. **EC and checksums**: GPU I/O is rejected on EC objects and on updates to checksum or dedup
   containers. DFS files are EC by default in a redundant container, the prototype's example
   uses `EC_4P2G1`, and `dfs_suggest_oclass()` may pick EC. Create files with an explicit
   replicated or non-redundant class, and create the container with checksums off
   (`cksum:off`; they are on by default) and without dedup. Replicated classes are still to
   be proven on hardware (see the risks of [gpuio.md](gpuio.md)). Server-side
   EC ([server_ec.md](server_ec.md)) lifts the EC restriction for arrays.
7. **Missing key**: `dfs_lookup_rel()` returns `ENOENT`; `prepXfer` fails and `queryMem` reports
   the key absent.

#### Event Queue Lanes

A lane is one DAOS event queue plus the thread that polls it (`num_event_queues`, default 1;
`max_inflight_per_queue`, default 1024). In DAOS each event queue is a CaRT context with its own
network domain, so every lane pins a registered region once more.

**Restriction**: with `VRAM_SEG`, use one lane per process, and scale with one process per GPU,
the usual layout of vLLM and Dynamo workers.

- With N lanes the pool is pinned N times; a lane that hits a NIC or BAR1 limit falls back to
  per-I/O registration (slower, not wrong).
- Call `daos_mem_bind()` for the lane at `registerMem`, so that the first transfer does not
  pay for the registration, and a pool too large to register is reported then.
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
  domain of its event queue (`daos_eq_net_domain()`); `deregisterMem` calls
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
- Keep `num_event_queues` at 1 when GPU memory is registered.

#### Expected Benefit

- **GPU memory**: no staging copy in the framework. Expect a host memory bandwidth gain, not a
  throughput gain; see the external measurement in
  [gpuio.md](gpuio.md#testing--validation).
- **Host memory**: the plain DFS calls already do RDMA from the caller's buffer. A registered
  region only saves the registration per I/O, which the transport cache already avoids where
  it is on; where it is off (verbs clients by default), `daos_mem_register()` returns
  `-DER_NOSYS`.
- **Not changed by `_ma`**: the metadata cost per block (rules 1 and 4), which probably
  dominates for small blocks.

### LMCache

LMCache can address storage by key, so the NIXL backend serves it. Its NIXL adapters check the
backend name against fixed lists, which also decide whether a backend may be given GPU memory;
DAOS has to be added to them upstream. A native connector, without NIXL, is an alternative
(see [Alternatives Considered](#alternatives-considered)).

### Dynamo

Dynamo's KV block manager has four tiers (from its source, `lib/kvbm-*` and `lib/memory`):

| Tier | Where | NIXL type | How it is reached |
|------|-------|-----------|-------------------|
| G1 | GPU memory | `VRAM_SEG` | CUDA copy to G2; NIXL to G3 or to a remote worker |
| G2 | pinned host memory | `DRAM_SEG` | NIXL to G3 or to a remote worker |
| G3 | disk: one preallocated local file per worker | `FILE_SEG` | NIXL POSIX backend, or GDS |
| G4 | shared object store | not NIXL | Dynamo's own S3 client, key per block |

So the NIXL backend does not reach Dynamo today, although DAOS needs no further API for it. The
routes:

| Route | Tier | Memory | Needs |
|-------|------|--------|-------|
| dfuse mount as the disk cache directory | G3 | host | nothing in DAOS or NIXL; not tried |
| S3 endpoint on DAOS | G4 | host | a gateway with conditional PUT (`src/client/ds3` has none) |
| NIXL object storage with the NIXL backend | G4 | host, GPU | Dynamo to use NIXL for G4 |
| `FILE_SEG` in the NIXL backend | G3 | host, GPU | path mode, or the descriptor mapping |

G4 is the shared, persistent tier and the one the NIXL backend fits; it depends on Dynamo. G3 is
a per-worker scratch file, for which the dfuse mount is the simple answer.

### Mooncake Store

Mooncake Store offloads values from its memory tier to a storage backend
(`StorageBackendInterface`): a batch offload of key to a list of slices (pointer and size), a
batch load, a load of a byte window, existence, size, scan and eviction. Nothing more is
needed from DAOS for it. There are three ways in, from the least code to the most:

| Route | Mooncake side | DAOS side |
|-------|---------------|-----------|
| dfuse mount | its POSIX file adapter, no code | file I/O through dfuse, host memory |
| object adapter | a DAOS `ObjectStorageAdapter`: put, get, range get, delete, list | object APIs |
| native backend | a DAOS `StorageBackendInterface` backend | object APIs, events |

For the last two, each value is one record under a binary dkey and akey, written from the
slices as one multi-iov sgl with asynchronous object I/O. A byte window needs an array value,
since a single value cannot be fetched in part.

**Host memory** works with the existing object calls. The slices come from Mooncake's pinned
host pools, so the backend can register those pools once with `daos_mem_register()` (host
attributes) and pass `ma_handle`:

- it needs the registration cache on (see [gpuio.md](gpuio.md#memory-registration));
- a host region takes one slot of the bulk quota of its context, and is not registered over
  the quota, so a backend with many pools raises the quota or registers fewer, larger regions;
- Mooncake's transfer engine registers the same pools with the NIC for its own RDMA; the DAOS
  registration is a second one in another network domain. The engine's key cannot be reused
  (see [gpuio.md](gpuio.md#a7-import-an-rdma-key-registered-by-another-library)).

**GPU memory** does not reach a backend today: Mooncake finds device pointers itself, copies
them to a pinned host pool and hands the backend host slices (a slice has no memory type).
With a Mooncake change that skips this copy for a capable backend, the backend uses
`daos_obj_{fetch,update}_ma()` with the device id Mooncake already has. The limits:

- one sgl has one memory type, so a value whose slices mix host and device memory cannot be
  one GPU I/O; the backend stages the device slices, or stores the value in parts;
- Mooncake also supports accelerators that have no DAOS memory type and no libfabric support
  (Ascend); those stay on the host copy;
- loads go into a host buffer today, so GPU loads also need a device buffer on Mooncake's side;
- the rules of [gpuio.md](gpuio.md#client-side-safety-checks) apply: no EC, a container with
  checksums off for updates, and replicated updates still to be proven on hardware.

### cuFile and cuObject

Not covered by the current phases. Both libraries register the GPU buffer themselves and pass
the storage system an RDMA descriptor for a transfer that the storage server starts over the
DC (dynamically connected) transport. NVIDIA documents this model for cuObject: the callback
must pass the descriptor to NVIDIA's server library, which does the RDMA. The descriptor
cannot be used on a DAOS connection, see A.7 in
[gpuio.md](gpuio.md#a7-import-an-rdma-key-registered-by-another-library).

A route therefore has to hand the GPU buffer itself to DAOS, which registers it on its own
connection and ignores the descriptor. The candidate routes below all do that, so none of
them needs a server or wire change. A server change is needed only to use NVIDIA's
descriptor as it is meant, which is a different design (last paragraph).

**cuFile** has two candidate routes on the current APIs. Neither has been run on a GPU:

1. **A cuFile userspace plugin** (`CUfileFSOps_t`, file handle type
   `CU_FILE_HANDLE_TYPE_USERSPACE_FS`). The application keeps NVIDIA's `libcufile.so` and
   registers the DAOS callbacks for its file. The prototype implements this
   (`src/client/cufile/` on its branch), and it ports to this design by calling
   `dfs_{read,write}_ma()` in the callbacks; the only feature lost is the import of the
   cuFile key, so DAOS registers the buffer itself.
   - The interface has no buffer registration hook, so the plugin cannot hold a
     `daos_mem_register()` region safely: every I/O registers its buffer, and the memory is
     pinned by cuFile and by DAOS.
   - **Open question, which decides the route**: what the callback buffer is when cuFile
     passes an RDMA descriptor. The prototype passes it to DAOS as a CUDA device address,
     while its own document describes it as always host-accessible memory (a bounce buffer
     or a BAR1 mapping of the GPU buffer). A BAR1 mapping is not CUDA memory for the
     transport. The prototype has no test of this path.
   - Without a descriptor cuFile stages the data in a host buffer and the plugin uses the
     host calls. That works today and is not GPU direct I/O.
2. **A DAOS-provided `libcufile.so`** that implements the cuFile calls for DAOS files and
   forwards other files to NVIDIA's library. `cuFileBufRegister()` maps to
   `daos_mem_register()` and `daos_mem_bind()`, `cuFileRead()` and `cuFileWrite()` to
   `dfs_{read,write}_ma()` with the device address the application passed, so there is one
   registration, held as a region. The cost is that it replaces NVIDIA's library: it has to
   follow the `cufile.h` ABI and be loaded in its place.

Both routes need the following, in the plugin or the shim, which may link with CUDA:

- A mapping from the file descriptor of a dfuse file to the DFS file; see the
  [appendix](#appendix-resolving-a-dfuse-file-descriptor).
- A host fallback where a GPU I/O is rejected: a hole, an EC file, an update on a container
  with checksums (see [gpuio.md](gpuio.md#errors-and-fallback)).
- The device of the buffer, for `ma_device_id`; the prototype always passes device 0.

**cuObject** has one candidate route, the same idea as the cuFile plugin. The application
gives `cuObjClient` a get and a put callback, which receive a buffer, a size, an offset and
the RDMA descriptor. The callbacks ignore the descriptor and call
`daos_obj_{fetch,update}_ma()` on the buffer; the prototype has a design for this and no
code. It shares the limits of the cuFile plugin: no registration hook, so a registration per
I/O and a second pin, and the same open question about what the callback buffer is. It also
needs a mapping from the cuObject key to a DAOS object, dkey and akey.

**Not a route here: using the descriptor.** The model NVIDIA documents has the storage
server start the RDMA from the descriptor, with NVIDIA's server library, over DC. In DAOS
that means the descriptor travels in the RPC and the server does the RDMA into or out of
its I/O buffer in place of the bulk transfer: a server and wire change, for NVIDIA NICs
only. It would remove the second registration, and it is a Non-Goal of
[gpuio.md](gpuio.md#non-goals), so choosing it means a new design.

## Implementation Phases

NIXL backend (NIXL repository):

1. **Host memory** (the upstream PR): keep it host only so that it can merge, with rules 1 and
   2, which are independent of GPU support, then rule 4.
2. **GPU memory**: add `VRAM_SEG` on the DFS `_ma` calls (phase 3 of
   [gpuio.md](gpuio.md#implementation-phases)) once a test on a GPU node passes, with the
   one-lane restriction.
3. **EC files**: after server-side EC for arrays ([server_ec.md](server_ec.md), phase 1).

Others:

- **LMCache**: DAOS in its backend name lists, with phase 1 or 2 of the NIXL backend.
- **Dynamo**: no work planned; it depends on the route chosen in [Dynamo](#dynamo).
- **Mooncake**: dfuse works without code; a host adapter or backend can be written today;
  the GPU path follows Mooncake.
- **cuFile**: future. First step: one test on a GPU node of what the plugin callback gets
  as its buffer, which decides between the two routes. **cuObject**: future; the same test
  on its callbacks.

## Compatibility & On-Disk Impact

None in DAOS. NIXL blocks are ordinary DFS files that any DFS client can read. See
[gpuio.md](gpuio.md#compatibility--on-disk-impact).

## External Interfaces

None in DAOS. The integrations use the APIs listed in
[gpuio.md](gpuio.md#external-interfaces). The parameters of the NIXL backend are those of the
prototype.

## Testing & Validation

NIXL backend, in the NIXL repository, on a GPU node:

- A NIXL transfer test with `DRAM_SEG` and `VRAM_SEG` against a DAOS container.
- Rule 1: an overwrite with a smaller block followed by a read with the larger buffer misses; a
  key whose write was prepared but not posted is not reported present; a reader during an
  overwrite gets the old block, the new one or a miss; two writers of one key both succeed.
- Rule 7: a missing file is reported absent.
- `VRAM_SEG` on a non-redundant and on a replicated object class, on OFI/verbs and UCX.
- An LMCache or Dynamo run that offloads and reloads KV blocks.
- Throughput and host memory traffic of `VRAM_SEG` against host staging; the cost of the rename
  per block against a single large chunk (rules 1 and 2), for small and large blocks.

## Risks, Mitigations and Future Works

### Risks and Mitigations

- **Space reclaim is not solved.** Dynamo and LMCache evict blocks, but the NIXL backend
  interface has no delete call, so nothing removes the files and the container fills up.
  Options: a delete call added to the NIXL backend interface upstream; the framework removing
  files itself, through a dfuse mount or libdfs; or a cleanup tool on the DAOS side.
- **Upstream dependencies**: the rules and the lane restriction have to be accepted on the NIXL
  PR, LMCache needs DAOS in its backend name lists, and Dynamo needs one of the routes in
  [Dynamo](#dynamo).
- **Temporary files** of writers that died (rule 1) add to the space that has to be reclaimed.
- **No data integrity check** without client checksums; deployers must accept this for KV-cache
  containers.

### Future Works

- `FILE_SEG` in this backend, on the same `dfs_*` calls: path mode first, descriptor mode if
  a framework opens its files itself (see [Where DAOS Fits](#where-daos-fits) and
  [Dynamo](#dynamo)). For GPU memory the alternative is a cuFile route under NIXL's
  `cuda_gds` backend; one of the two is enough.
- An object API mode for small blocks, if the DFS cost per block is confirmed (see the
  [appendix](#appendix-the-two-nixl-backends-for-daos)).
- cuFile and cuObject through one of the client-only routes (see
  [cuFile and cuObject](#cufile-and-cuobject)).
- The GPU path of the Mooncake backend, once Mooncake has a device memory path.

## Alternatives Considered

- **One single value per block through the object API instead of DFS files**: a single value
  is atomic, so it avoids the create and the rename per block (rule 1). Files were chosen, as
  in the prototype, for offsets, a namespace, dfuse visibility and one set of APIs shared with
  `FILE_SEG`. An external plugin takes the object route with dkeys and akeys and reports a
  much lower fixed cost per block; see the
  [appendix](#appendix-the-two-nixl-backends-for-daos).
- **`chunk_size` at least the largest block as the only rule**: every block is then a single
  update, but the whole block goes to one target in one stream. The prototype and the external
  test run with chunks smaller than the block, so this is kept for small blocks only (rule 2).
- **LMCache native DFS connector** (`csrc/storage_backends`, modeled on `fs` and `hf3fs`): avoids
  the NIXL dependency and the backend-name list, but serves LMCache only, and the native
  connectors move host memory only today. One exists outside LMCache (see
  [Related Efforts](#related-efforts)). Possible later, on the same DFS `_ma` APIs.

## Appendix: The Two NIXL Backends for DAOS

Two backends were written independently. This document specifies the first; the second is
described here from its README and source, and its figures are quoted as reported, not
verified.

| | On libdfs | On the object API |
|-|-----------|-------------------|
| Source | NIXL PR 2261, open | `gluesys/nixl-daos` |
| A key is | a DFS file in a directory | an object; its id comes from the NIXL `devId` |
| Layout | file offset, DFS chunks | dkey = offset / 64 MiB, akey = the rest |
| Requests | one DFS read or write per descriptor | a descriptor list in one fetch or update |
| Event queues | lanes, default 1 | one borrowed per request from a pool |
| Dead engine | no deadline | polls with a deadline (60 s by default) |
| GPU memory | no | `VRAM_SEG` on the DAOS prototype's calls, off |
| Other DAOS clients | files visible through DFS and dfuse | only through the plugin's mapping |
| Atomic replace | by rename (rule 1) | none described |
| Needs for GPU | DFS `_ma` (phase 3) | object `_ma` (phase 2, implemented) |

Sources: [NIXL PR 2261](https://github.com/ai-dynamo/nixl/pull/2261) and
[gluesys/nixl-daos](https://github.com/gluesys/nixl-daos).

What the object backend reports:

- A fixed cost per 1 MiB object of 0.63 ms through DFS against 0.014 ms through the object API.
- For GPU memory, about 0.43 ms per sgl entry for the registration per I/O, which makes
  `VRAM_SEG` 3.4 times slower than host memory there. This is the cost `daos_mem_register()`
  is meant to remove.
- One event queue per thread was slower than blocking calls, because every idle queue holds a
  network context; borrowing a queue per request fixed it.

What it means for this design:

- The object backend can use phase 2 of [gpuio.md](gpuio.md) as it is: rename
  the calls, use the new `daos_mem_attr_t`, and call `daos_mem_register()` and
  `daos_mem_bind()` in `registerMem`.
- Its borrowed event queues conflict with a registered region, which is pinned once per event
  queue: for `VRAM_SEG` it needs one queue, as in [Event Queue Lanes](#event-queue-lanes).
- One backend is enough, and two would not interoperate: the layouts differ, so data written
  by one cannot be read by the other. The libdfs backend is the base, since its files are
  visible to every DFS and dfuse client and it can serve both storage types. Worth taking
  from the object backend: its GPU memory experience, the polling deadline, and several
  descriptors in one request.
- If the DFS cost per object holds, the object route (first item of
  [Alternatives Considered](#alternatives-considered)) is the faster one for small blocks, at
  the price of the namespace, the atomic replace and the visibility that DFS gives.

## Appendix: Resolving a dfuse File Descriptor

A cuFile route, and the descriptor mode of `FILE_SEG`, start from a file that the
application opened through a dfuse mount, and have to do their I/O through libdfs, which
needs a `dfs_t` and a `dfs_obj_t`. dfuse hands them out through ioctls on the file descriptor
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
- [Mooncake](https://github.com/kvcache-ai/Mooncake)
- [NVIDIA GDS overview](https://docs.nvidia.com/gpudirect-storage/overview-guide/) (cuFile),
  [NVIDIA cuObject](https://docs.nvidia.com/gpudirect-storage/cuobject/index.html)
