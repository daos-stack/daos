# GPU Direct I/O Support (Draft) Design

## Summary

- **Problem**: GPU workloads (AI/ML training and inference, HPC) stage data through a host bounce
  buffer (`GPU -> cudaMemcpy -> host buffer -> DAOS client -> network -> server`). The extra copy
  costs host memory bandwidth and latency.
- **Proposal**: let applications pass GPU buffers to the DAOS object, array and DFS APIs, so that
  the NIC moves data between GPU memory and the server with GPUDirect RDMA:
  1. Object APIs `daos_obj_{fetch,update}_ma()` (`_ma`: memory attributes) take a
     `daos_mem_attr_t` side-channel that describes the buffers. The API is not GPU specific.
  2. CaRT creates bulk handles with a memory type and device (`crt_bulk_create_with_mdev()`), so
     that the transport registers GPU memory with the NIC.
  3. `daos_mem_register()` registers a large buffer, such as a GPU pool, once, so that
     block-sized I/O inside it does not register per I/O; `daos_mem_bind()` does the
     registration for an event queue ahead of the first I/O.
  4. Array and DFS `_ma` APIs, which most consumers need.
- **Consumers**: the target is a generic DAOS backend for GPU data movers:
  [NIXL](https://github.com/ai-dynamo/nixl) and through it Dynamo and LMCache, Mooncake Store,
  and NVIDIA cuFile and cuObject. Every change is checked against NIXL and Mooncake, and
  so that cuFile and cuObject stay possible. The current phases serve the consumers
  that register their buffers through DAOS: NIXL backends (Dynamo, LMCache) and Mooncake.
  cuFile and cuObject have candidate routes on these APIs with no server change, none
  verified yet. The integrations are specified in [integrations.md](integrations.md).
  DAOS gets no KV-cache specific API, layout or server component.
- **Impact**: removes one full copy on the GPU I/O path. No wire protocol change and no effect on
  non-GPU I/O.

## Background

The client creates a bulk handle per sgl with `crt_bulk_create()` (`HG_Bulk_create()` on host
pointers) and inlines small I/O into the RPC. The server pulls or pushes the data with
`HG_Bulk_transfer()` and only sees an opaque bulk handle.

The lower layers already support device memory: libfabric registers GPU memory through
`FI_HMEM`, UCX detects CUDA memory, and Mercury 2.4 takes a memory type and device in
`HG_Bulk_create_attr()`. How the NIC reaches GPU memory depends on the GPU vendor (see
[GPU Vendors](#gpu-vendors)).

### Related Efforts

Work in progress that this design builds on or has to follow. Results are quoted as reported;
none has been reproduced for this design. The consumer side (NIXL backends, LMCache, Dynamo,
Mooncake) is listed in [integrations.md](integrations.md#related-efforts).

- **Prototype** ([daos-stack/daos#18566](https://github.com/daos-stack/daos/pull/18566), branch
  `theodore/b_cufile`, open): object `_gpu` calls, an import of the cuFile RDMA key,
  `dfs_{read,write}_gpu()` and a cuFile plugin. It carries no test results. This design
  replaces it; see [A.7](#a7-import-an-rdma-key-registered-by-another-library) for the key
  import.
- **External test** ([gluesys/lmcache-daos](https://github.com/gluesys/lmcache-daos)): a GPU
  backend for LMCache on the prototype's DFS calls. It ran on one GPU and one client, on a
  build of the prototype with seven local patches, and on a platform without `nvidia-peermem`
  (dma-buf registration). It is the source of the measurements, of the `nvidia-peermem` and
  dma-buf findings, of the replicated update failure (a report not yet filed,
  `doc/upstream/daos-gds-replicated-write-report.md` there) and of a failure mode study (I/O
  to a dead engine does not return). This document calls it "the external test".
- **libfabric dma-buf fix** ([ofiwg/libfabric#12828](https://github.com/ofiwg/libfabric/pull/12828),
  merged, from the external test): the verbs provider releases the dma-buf file descriptor
  after a registration. It is not in the pinned v1.22.0 and does not apply to it (it uses a
  helper added in libfabric 2.1), so DAOS does not carry it as a patch; see
  [GPU Vendors](#gpu-vendors).
- **Checksums by default** (DAOS-19451, merged): new containers have checksums on, which
  GPU updates cannot use yet. A container for GPU updates must be created with checksums
  off; see [Client-Side Safety Checks](#client-side-safety-checks).

## Goals and Non-Goals

### Goals

- **Zero host copy** between GPU memory and the DAOS server.
- **No wire or ABI change**: memory attributes never leave the client; existing APIs unchanged.
- **Generic backend**: one set of object, array and DFS APIs for every consumer that hands
  its buffers to DAOS. cuFile and cuObject pass their own RDMA descriptor, which DAOS cannot
  use. Both can still reach these APIs where the GPU buffer itself is handed to DAOS
  (candidate routes, not verified and not part of the current phases).
- **Register once**: a consumer that registers a pool does not pay a registration per block.
- **Transport agnostic**: the same APIs and function on libfabric (OFI) and UCX. Performance
  is per transport and is measured, not promised to be equal.
- **Vendor neutral**: the APIs depend on the memory type only, and CUDA, ROCm and Level Zero
  are in the enum; see [GPU Vendors](#gpu-vendors) for the status of each.

### Non-Goals

- Server-side GPU buffers: the server data path is unchanged; it only logs and counts GPU I/O.
- A second data path in the server for transfers started from a cuFile or cuObject descriptor
  (see [Future Works](#future-works)).
- Client-side EC encoding and client-side checksums for GPU buffers: the client CPU cannot read
  GPU memory. Server-side EC is a separate design ([server_ec.md](server_ec.md));
  user-supplied checksums are [Future Works](#future-works).
- KV-cache semantics (keys, dedup, tiering, eviction): they stay in the frameworks.
- Topology-aware NIC selection.

## Design

### Overview

```
Application   daos_mem_register() once per pool, daos_mem_bind() per event queue (optional)
              dfs_{read,write}_ma() / daos_array_{read,write}_ma() / daos_obj_{fetch,update}_ma()
              + daos_mem_attr_t (type, device, handle)
                   |
DAOS client   DFS and array: one object I/O per chunk, same attributes; no CPU zeroing of holes
              object: validate attributes, safety checks, ORF_GPU_DIRECT
              obj_rw_bulk_prep() -> crt_mem_bind(), crt_bulk_create_with_mdev()
                   |
CaRT          registered regions (crt_mem_*): one held bulk per region and context
              HG_Bulk_create_attr() with memory type and device
                   |
Mercury       libfabric FI_HMEM / UCX; the MR cache reuses a held pool registration
                   |
DAOS server   data path unchanged, RDMA to or from GPU memory is transparent
```

### Memory Attributes

```c
typedef enum {
	DAOS_MEM_TYPE_HOST = 0, /**< System/host memory (default) */
	DAOS_MEM_TYPE_CUDA = 1, /**< NVIDIA CUDA device memory (cudaMalloc()) */
	DAOS_MEM_TYPE_ROCM = 2, /**< AMD ROCm device memory */
	DAOS_MEM_TYPE_ZE   = 3, /**< Intel Level Zero device memory */
} daos_mem_type_t;

/** Client side only, never sent to the server */
typedef struct {
	daos_mem_type_t   ma_mem_type;  /**< memory type of all buffers in the sgl */
	uint64_t          ma_device_id; /**< device ordinal in the process (CUDA device index) */
	daos_mem_handle_t ma_handle;    /**< optional region from daos_mem_register() */
	d_iov_t           ma_csums;     /**< reserved for user data checksums, must be empty */
} daos_mem_attr_t;
```

- A side-channel instead of a memory type in `d_iov_t`, which is encoded in RPCs and used by
  the whole stack (see [Alternatives](#alternatives-considered)).
- `mem_attrs` has one entry per sgl, or is NULL for host memory. The client copies it.
- Entries may differ in type and device, for example a host header and a GPU payload in one
  update. If any entry is device memory, the whole call is a GPU I/O; there is no flag to
  pass. A call with host entries only takes the regular path (inline, checksums, EC).
- `DAOS_MEM_TYPE_CUDA` is memory from `cudaMalloc()` (or `cuMemCreate()` with
  `gpuDirectRDMACapable`). Managed memory (`cudaMallocManaged()`) is one pointer whose pages
  the driver moves between host and GPU on access. It has no fixed GPU pages for the NIC to
  pin, so it cannot be a GPUDirect RDMA target and has no memory type.
- `ma_device_id` is the device as the caller sees it (after `CUDA_VISIBLE_DEVICES`); HOST entries
  require 0.
- `ma_csums` is reserved now so that user-supplied checksums need no ABI change later.

### Public APIs

```c
int daos_obj_fetch_ma(daos_handle_t oh, daos_handle_t th, uint64_t flags, daos_key_t *dkey,
		      unsigned int nr, daos_iod_t *iods, d_sg_list_t *sgls,
		      const daos_mem_attr_t *mem_attrs, daos_iom_t *ioms, daos_event_t *ev);
int daos_obj_update_ma(daos_handle_t oh, daos_handle_t th, uint64_t flags, daos_key_t *dkey,
		       unsigned int nr, daos_iod_t *iods, d_sg_list_t *sgls,
		       const daos_mem_attr_t *mem_attrs, daos_event_t *ev);

int daos_mem_register(void *buf, size_t len, const daos_mem_attr_t *attr, daos_mem_handle_t *mh);
int daos_eq_net_domain(daos_handle_t eqh, daos_net_domain_t *domain);
int daos_mem_bind(daos_mem_handle_t mh, daos_net_domain_t domain);
int daos_mem_deregister(daos_mem_handle_t mh);
```

Typical use: register a GPU pool once, then do block I/O anywhere inside it.

```c
daos_mem_attr_t   ma = { .ma_mem_type = DAOS_MEM_TYPE_CUDA, .ma_device_id = dev };
daos_mem_handle_t mh;
daos_net_domain_t domain;

rc = daos_mem_register(pool_base, pool_len, &ma, &mh);         /* once, at startup */
rc = daos_eq_net_domain(eqh, &domain);       /* optional: register now, not on the first I/O */
rc = daos_mem_bind(mh, domain);

d_iov_set(&iov, pool_base + blk * blk_size, blk_size);         /* per I/O */
ma.ma_handle = mh;
rc = daos_obj_update_ma(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl, &ma, ev);

rc = daos_mem_deregister(mh);   /* after the last I/O, before cudaFree() */
```

The sgl may hold several iovs anywhere inside the region. The handle is a hint: I/O without it,
or where the region could not be registered, registers its own buffers.

A region has to be registered with the network once per network domain, and each event queue
has its own. `daos_net_domain_t` is that domain: an opaque pointer (the CaRT context of the
event queue today), valid until its event queue is destroyed. `daos_eq_net_domain()` returns it
for an event queue, or for blocking calls with `DAOS_HDL_INVAL`. `daos_mem_bind()` is optional
because the first I/O through the handle on a domain registers the region there anyway; the
call only does it earlier and returns the error (see
[Memory Registration](#memory-registration)).

Rules for the caller, which DAOS cannot check since it does not link with CUDA:

- Set `CU_POINTER_ATTRIBUTE_SYNC_MEMOPS` on the buffer, so that GPU operations on it are ordered
  with the RDMA (UCX and NIXL do).
- **CUDA context**: the thread that calls a `_ma` I/O or `daos_mem_bind()` registers the
  memory, and must have the CUDA context of the device current (`cuCtxSetCurrent()`). So must
  the thread that polls the event queue: an update that is retried (for example after a pool
  map change) creates its bulk again there. Without the context the I/O fails or registers
  again on every call, depending on the platform. To be confirmed on hardware.
- **No abort**: `daos_event_abort()` does not cancel an I/O today (it returns 0 and does
  nothing), so after it the server may still read or write the buffer. A buffer must not be
  reused or freed until its event completes, whatever the caller did to give up on it.

### CaRT APIs

```c
typedef struct {
	d_mem_type_t md_type;      /**< D_MEM_TYPE_{HOST,CUDA,ROCM,ZE} */
	uint64_t     md_device_id;
} d_mem_dev_t;

int crt_bulk_create_with_mdev(crt_context_t crt_ctx, d_sg_list_t *sgl, crt_bulk_perm_t bulk_perm,
			      const d_mem_dev_t *mem_dev, crt_bulk_t *bulk_hdl);

int crt_mem_register(void *buf, size_t len, const d_mem_dev_t *mem_dev, crt_mem_handle_t *mh);
int crt_mem_bind(crt_mem_handle_t mh, crt_context_t crt_ctx);
int crt_mem_deregister(crt_mem_handle_t mh);
void crt_mem_addref(crt_mem_handle_t mh);
void crt_mem_decref(crt_mem_handle_t mh);
int crt_mem_query(crt_mem_handle_t mh, void **buf, size_t *len, d_mem_dev_t *mem_dev);
```

`crt_bulk_create()` is unchanged and calls `crt_bulk_create_with_mdev()` with NULL. CaRT and gurt
only know the memory device (`d_mem_dev_t`: type and device id); `daos_mem_attr_t` belongs to
the DAOS API, and the object layer passes its type and device down. Device memory fails with
`-DER_NOTSUPPORTED`, instead of being registered as host memory, when Mercury lacks bulk
attributes or CaRT runs without memory device support (`D_MEM_DEVICE`).

Registered regions are implemented in CaRT (`crt_mem_*`), since they only deal with contexts,
bulk handles, the bulk quota and the transport cache; see
[Memory Registration](#memory-registration). `crt_context_destroy()` drops the registrations
of its context. The DAOS calls are wrappers: `daos_mem_register()`, `daos_mem_bind()` and
`daos_mem_deregister()` map the DAOS types. They are not object calls, since the array and DFS
APIs use them too, so they are declared with the memory attributes in a header of their own
(`daos_mem.h`, included by `daos_obj.h`) and live in the client API library
(`src/client/api/mem.c`). The object layer keeps what is about I/O: it
checks that the iovs are inside the region (`crt_mem_query()`), holds a reference on the handle
for each I/O in flight (`crt_mem_addref()`), and calls `crt_mem_bind()` for the context of the
I/O before it creates the bulk.

### Client-Side Safety Checks

For a GPU I/O the client:

1. **Validates the attributes** (every `_ma` call): known memory type, device id 0 for HOST,
   and for `ma_handle` a matching type and device and every iov inside the region.
   Failure: `-DER_INVAL`.

2. **Forces bulk transfer**. A GPU pointer is not mapped for the CPU, so inlining, which copies
   the data into the RPC with `memcpy()`, would fault. A copy through the GPU runtime would be
   the copy this design removes, and DAOS does not link with CUDA. As a guard, a GPU I/O that
   would still be sent inline fails with `-DER_INVAL`, and a reply that carries inline data
   for one fails with `-DER_PROTO`, instead of copying.

3. **Skips data checksums**, since computing or verifying them reads the buffers with the CPU.
   An update on a checksum or dedup container is rejected, because the server requires client
   checksums. A fetch from a checksum container is allowed and returns the data without
   verification; the client logs a warning once per container handle. (Lustre also disables
   checksums for GPU pages.) New containers have checksums on by default, so a container for
   GPU updates must be created with checksums off (`cksum:off`) until user-supplied or
   server-side checksums exist ([Future Works](#future-works)). `ma_csums` is reserved for
   the former: a call that sets it today fails with `-DER_INVAL`, on any container.

4. **Rejects EC objects**, which the client encodes and decodes with the CPU. This is the common
   case, not a corner case: see [DFS and Array APIs](#dfs-and-array-apis).

5. **Rejects updates in a transaction**, including updates converted to one: the compound RPC
   at commit builds its bulk without attributes and inlines small updates. No consumer needs
   them. A fetch with a transaction handle is allowed; it is not queued.

6. **Never merges iovs** into a host buffer when an entry is device memory or has `ma_handle`;
   neither the CPU nor the region covers a merged buffer. Copied sgls still point at the user
   buffers and keep their attributes; only EC reassembled sgls drop them. The region of
   `ma_handle` is checked on the first attempt only, not on retries.

`ORF_GPU_DIRECT` marks the RPC for the server, which only logs it and counts it in a telemetry
counter (`gpu_direct`). The object layer derives the internal `DAOS_OBJ_IO_GPU_DIRECT` flag from
the attributes on the first attempt of every fetch and update, also through the task API. The
flag is not in the public headers, and a value passed by the caller is ignored.

### Memory Registration

Without a handle, every GPU I/O registers its buffers with the NIC and deregisters them at
completion, which dominates the latency of small blocks. Neither Mercury nor CaRT caches
registrations; the transport does (libfabric MR cache, UCX rcache). `daos_mem_register()` uses it:

- `daos_mem_register()` only records the region. Nothing is registered with the network yet.
- The region is registered once per network domain, because the transport cache belongs to a
  domain and each event queue has its own (with its own CaRT context and Mercury class). The
  client creates one bulk over the whole region there and holds it, so the region stays an
  in-use cache entry. Later I/O creates a bulk on its block as usual; the cache finds it inside
  the held entry, so only a lookup and a Mercury handle remain.
- That registration happens on the first I/O through the handle on a domain, or earlier with
  `daos_mem_bind(mh, domain)`. The call moves the cost of registering a large pool out of the
  first transfer, and it returns the error when the region cannot be registered, which the
  first I/O only logs. A region can be bound to several domains, and there is no unbind: the
  binding ends with the deregistration or the event queue.
- Every domain in use pins the region once, so GPU consumers should use one event queue per
  process. The long-term fix is one domain for all event queues
  ([Future Works](#future-works)); `daos_net_domain_t` is opaque so that the calls stay the
  same then.
- A region that cannot be registered on a context (BAR1 or NIC limits) is logged once and
  remembered, `daos_mem_bind()` returns the error, and I/O there registers its own blocks.
- A registered region holds one slot of the client bulk quota of its context (`D_QUOTA_BULKS`,
  64 by default). A device memory region is registered even over the quota, like every device
  memory bulk (see the CUDA context rule in [Public APIs](#public-apis)). A host memory region
  over the quota is not registered (`daos_mem_bind()` returns `-DER_AGAIN`), I/O registers its
  own blocks, and a later I/O tries again. A consumer with many regions should register one
  large pool, or raise the quota.
- Destroying an event queue drops its registrations; the handle stays usable. An event queue
  must not be destroyed, even with `DAOS_EQ_DESTROY_FORCE`, while I/O on it is running: a first
  registration in progress would be inserted after the teardown and freed later on a destroyed
  Mercury class.
- `daos_mem_deregister()` returns `-DER_BUSY` while an I/O holds the handle.
- `daos_mem_register()` fails if a region would be of no use (see
  [Errors and Fallback](#errors-and-fallback)). The MR cache is disabled by default for
  `ofi+verbs;ofi_rxm` clients (set `CRT_MRC_ENABLE=1`, process wide). libfabric caches per
  memory type, so the check (`crt_mr_cache_check()`) is made for the type of the region: a
  CUDA region needs the CUDA cache monitor (`FI_MR_CUDA_CACHE_MONITOR_ENABLED`, on by
  default), not the host one.

No wire change. Cache behavior for GPU memory (hits inside a held entry, eviction, the CUDA
memory monitor) is not an API contract and must be checked on hardware; see the gate in
[Implementation Phases](#implementation-phases).

### DFS and Array APIs

Phase 3:

```c
int daos_array_read_ma(daos_handle_t oh, daos_handle_t th, daos_array_iod_t *iod,
		       d_sg_list_t *sgl, const daos_mem_attr_t *mem_attr, daos_event_t *ev);
int daos_array_write_ma(daos_handle_t oh, daos_handle_t th, daos_array_iod_t *iod,
			d_sg_list_t *sgl, const daos_mem_attr_t *mem_attr, daos_event_t *ev);
int dfs_read_ma(dfs_t *dfs, dfs_obj_t *obj, d_sg_list_t *sgl, const daos_mem_attr_t *mem_attr,
		daos_off_t off, daos_size_t *read_size, daos_event_t *ev);
int dfs_write_ma(dfs_t *dfs, dfs_obj_t *obj, d_sg_list_t *sgl, const daos_mem_attr_t *mem_attr,
		 daos_off_t off, daos_event_t *ev);
```

- One attribute per call (one sgl). The per-chunk sub-I/Os pass it on unchanged; their sgls stay
  inside the caller's buffers, so `ma_handle` remains valid.
- The array layer validates the attribute, copies it and holds the registered region until
  the whole call completes, since the per-chunk I/Os may start after the caller returned.
- Vectored DFS I/O (`dfs_readx()`, `dfs_writex()`) has no `_ma` variant yet; `dfs_readx_ma()`
  and `dfs_writex_ma()` may be added later.
- **Transaction handles** as in the host calls: `dfs_write_ma()` writes with `DAOS_TX_NONE`,
  since a GPU update in a transaction is rejected, and `dfs_read_ma()` reads with the handle
  of the DFS mount (`dfs->th`), which a GPU fetch accepts.
- **Object class**: in a container with a redundancy factor of 1 or more and at least 3 fault
  domains, a file created without an explicit class is EC (`dc_set_oclass()`), and GPU I/O on
  it is rejected. Until server-side EC exists ([server_ec.md](server_ec.md)), the
  creator must set a replicated or non-redundant class (`dfs_open()` class argument, or the
  container's file class), and existing EC files need a host buffer.
- **Holes**: the array layer zeroes holes inside the file size with `memset()`. For device
  memory such a read fails with `-DER_NOTSUPPORTED`; DAOS takes no CUDA dependency. Other parts
  of the buffer may already hold data, so its content is undefined and the caller reads the
  whole range again with a host buffer. Sparse files therefore need a host fallback in the
  caller.
- **Short reads**: a read past the end of the file is short and leaves the rest of the buffer
  untouched, as for host memory.

### Errors and Fallback

DAOS never stages device memory through a host buffer. A consumer that wants a fallback retries
with host memory.

| Kind | Cause | Error |
|------|-------|-------|
| Configuration | no memory device support (`D_MEM_DEVICE` unset) | `-DER_NOTSUPPORTED` |
| Configuration | Mercury without bulk attributes | `-DER_NOTSUPPORTED` |
| Configuration | `daos_mem_register()` with the MR cache disabled | `-DER_NOSYS` |
| Per I/O | EC object | `-DER_NOTSUPPORTED` |
| Per I/O | update on a checksum or dedup container | `-DER_NOTSUPPORTED` |
| Per I/O | update in, or converted to, a transaction | `-DER_NOTSUPPORTED` |
| Per I/O | DFS or array read over a hole | `-DER_NOTSUPPORTED` |
| Caller bug | invalid attributes, iov outside the region | `-DER_INVAL` |
| Caller bug | deregister while I/O uses the handle | `-DER_BUSY` |

A consumer probes the configuration once at startup with `daos_mem_register()` of a device
buffer:

- `-DER_NOTSUPPORTED`: device memory cannot be used at all; use host memory.
- `-DER_NOSYS`: only registered regions are unavailable; do GPU I/O without `ma_handle`, which
  registers per I/O.

One case falls back to per-I/O registration with only a log line: a region that cannot be
registered on a context. A consumer that wants to know calls `daos_mem_bind()`, which returns
the error.

### Consumers

Each consumer reaches these APIs through a plugin, backend or connector of its own. They are
specified, with their status and open issues, in [integrations.md](integrations.md):

| Consumer | DAOS APIs it uses | Phase |
|----------|-------------------|-------|
| NIXL backend (LMCache; Dynamo by key) | DFS `_ma`, `daos_mem_register()` | 3 |
| Mooncake Store backend | object `_ma`, `daos_mem_register()` | 2 |
| cuFile | DFS `_ma`, through a shim or a plugin; not verified | future |
| cuObject | object `_ma`, through its callbacks; not verified | future |

### Build and Runtime Configuration

`scons BUILD_GPU_DIRECT=yes` builds libfabric (with `--enable-hook_hmem`) and UCX with GPU
memory support. The vendor options select the runtimes: `CUDA_PREFIX` (default
`/usr/local/cuda`; empty builds without CUDA) with `GDRCOPY_PREFIX` (default `/usr`), and
`ROCM_PREFIX` (empty by default). At run time set `D_MEM_DEVICE=1` or
`crt_init_options_t::cio_mem_device`; see `src/cart/README.env`.

| Dependency | Version |
|------------|---------|
| libfabric | >= 1.20 (pinned v1.22.0) |
| UCX | v1.20.0 |
| Mercury | v2.4.1 with the DAOS patches |
| GPU runtime | see [GPU Vendors](#gpu-vendors) |

### GPU Vendors

The APIs, the safety checks, registered regions and the DFS and array calls depend on the
memory type only, so the design is the same for every vendor, and Mercury passes all three
types on to the transport. What differs is below DAOS:

| | NVIDIA | AMD | Intel |
|-|--------|-----|-------|
| Memory type | `DAOS_MEM_TYPE_CUDA` | `DAOS_MEM_TYPE_ROCM` | `DAOS_MEM_TYPE_ZE` |
| Runtime | CUDA Toolkit >= 12.2, GDRCopy >= 2.3 | ROCm | Level Zero |
| Build option | `CUDA_PREFIX`, `GDRCOPY_PREFIX` | `ROCM_PREFIX` | none yet |
| How the NIC reaches the memory | `nvidia-peermem` module | dma-buf | dma-buf |
| Status | built, not run on hardware | not built | not built |

The two ways to register GPU memory with the NIC decide what a platform needs.

**Peer memory module (NVIDIA).** `nvidia-peermem` ships with the NVIDIA GPU driver and plugs
into a hook of the NVIDIA network driver stack (MLNX_OFED or DOCA-OFED). With it, a plain
`ibv_reg_mr()` on a GPU address works, which is what libfabric and UCX rely on for CUDA memory.
It must be loaded on the client; check it with `lsmod | grep nvidia_peermem`. The external test
(see [Related Efforts](#related-efforts)) reports that the network driver stack of DOCA 3.4 no
longer provides the hook, so the module cannot be loaded there, and CUDA memory would have to
be registered through dma-buf.

**dma-buf (AMD, Intel, and NVIDIA without the module).** The libfabric verbs provider registers
ROCm and Level Zero memory with `ibv_reg_dmabuf_mr()`. It needs a kernel, rdma-core and NIC
driver with dma-buf registration. The pinned dependencies have gaps on this path. DAOS carries
no patches for them; the fixes belong upstream:

- libfabric v1.22.0, verbs provider: it does not close the dma-buf file descriptor after a
  registration, so every registration leaks one descriptor. Where each I/O registers its
  buffers (no registered region, or the registration cache off), the process runs out of file
  descriptors; the external test reports that with a limit of 1024 it happens after about
  8 GiB read in 4 MiB blocks, and the transport then fails. The upstream fix (see
  [Related Efforts](#related-efforts)) does not apply to v1.22.0. Until the dependency moves,
  a client on this path needs registered regions and a high descriptor limit.
- libfabric v1.22.0, verbs provider: the dma-buf path is not used for CUDA memory.
- UCX v1.20.0: CUDA memory counts as registrable only if a peer memory module is found.
- Mercury v2.4.1, UCX plugin: the transport list is fixed to the provider name, which is
  reported to leave out UCX's CUDA memory components.

So **CUDA memory on a platform without `nvidia-peermem` is not supported**: a GPU I/O fails at
registration, and the consumer uses host memory.

Not worked out or not done yet, for all vendors unless noted:

- **Caller rules.** The rules of [Public APIs](#public-apis) (context of the registering
  thread, ordering of GPU operations with the RDMA) are written for CUDA; their ROCm and
  Level Zero equivalents are not worked out.
- **Build.** There is no option for Level Zero, and the ROCm option has not been built.
- **Tests and consumers.** Nothing has run on GPU hardware. `io_gpu_direct` loads the CUDA
  runtime only, and the consumers of [integrations.md](integrations.md) are CUDA oriented
  today.

## Implementation Phases

1. **CaRT** (implemented): `crt_bulk_create_with_mdev()`, registered regions (`crt_mem_*`),
   `BUILD_GPU_DIRECT`.
2. **Object** (implemented): `daos_obj_{fetch,update}_ma()`, safety checks, and the
   `daos_mem_register()` and `daos_mem_bind()` wrappers.
3. **DFS and array `_ma`** (implemented, not run on GPU hardware): what the NIXL backend on
   libdfs needs.
4. **EC for arrays**, server side ([server_ec.md](server_ec.md), phase 1): client
   only and no wire change. Needed with or right after phase 3, because DFS files are EC by
   default in redundant containers.
5. **Integrations** (in the consumers' repositories), see
   [integrations.md](integrations.md): the NIXL backend can add GPU memory on phase 3, with a
   replicated or non-redundant object class; a Mooncake backend can use phase 2.
6. **Future**: [Future Works](#future-works), including cuFile and cuObject.

**Hardware gate before phase 3 is merged**: the hardware checks of
[Testing & Validation](#testing--validation) pass on OFI/verbs and UCX. If the transport cache
cannot be relied on for GPU memory, registration switches to the block-offset alternative
(A.4 in [Alternatives](#alternatives-considered)).

## Compatibility & On-Disk Impact

- Persistent layout: no change.
- Wire protocol: no change. `ORF_GPU_DIRECT` (RPC flag bit 27) is advisory, used for a debug
  log and a telemetry counter; a server that does not know it ignores it.
  The internal `DAOS_OBJ_IO_GPU_DIRECT` (bit 32) is cleared from the flags sent to the server.
- Existing APIs and `d_iov_t` are unchanged. GPU I/O needs `BUILD_GPU_DIRECT=yes` and
  `D_MEM_DEVICE=1`.

## External Interfaces

- APIs: `daos_obj_{fetch,update}_ma()`, `daos_mem_{register,bind,deregister}()`,
  `daos_array_{read,write}_ma()`, `dfs_{read,write}_ma()`, `daos_eq_net_domain()`.
- Types: `daos_mem_type_t`, `daos_mem_attr_t`, `daos_mem_handle_t` (new header `daos_mem.h`),
  `daos_net_domain_t`. No public flag.
- Task API: `daos_obj_rw_t::mem_attrs` and `daos_array_io_t::mem_attr`, both added as the last
  field.
- CaRT: `crt_bulk_create_with_mdev()`, `d_mem_dev_t`, `crt_mem_{register,bind,deregister}()`,
  `crt_mem_{addref,decref,query}()`, `crt_mem_handle_t`, `crt_mr_cache_check()`,
  `crt_mem_device_check()`.
- Environment: `D_MEM_DEVICE` (existing). SCons: `BUILD_GPU_DIRECT`, `CUDA_PREFIX`,
  `GDRCOPY_PREFIX`, `ROCM_PREFIX`.

## Testing & Validation

- Unit tests (`cli_mem_attr_tests`, `eq_tests`): attribute validation and duplication, the handle
  reference, `daos_mem_bind()` and lazy per-context registration, their release on
  `daos_eq_destroy()`, a remembered registration failure, `-DER_BUSY` on deregister, a cache
  disabled from the environment.
- Functional tests (GPU node), on OFI/verbs and UCX. Today `io_gpu_direct` (IO58) covers a
  blocking round trip of one single value without a handle, in a container of its own with
  checksums off; the other cases are to be added:
  - update and fetch round trip, with and without `ma_handle`, on non-redundant and on
    replicated objects;
  - the rejections of [Errors and Fallback](#errors-and-fallback), including a DFS file with
    the default class in a redundant container;
  - conditional per-akey fetch keeps each akey's attributes;
  - the task API: device memory attributes without the flag, and the flag without attributes;
  - sgls with small iovs (no merge), `iov_len < iov_buf_len` and 0-length iovs (attributes kept),
    and a retried I/O with `ma_handle`;
  - over the bulk quota: a device memory bulk is created at once by the caller, a host memory
    region is retried by a later I/O, and a failed registration returns its quota slot;
  - DFS and array `_ma`: a block within a chunk and across chunks, a short read, a hole,
    `ma_handle` across the per-chunk split. `dfs_test_io_ma` (DFS_UNIT_TEST30) covers these
    with host memory attributes, plus the rejections that need no GPU; the device memory
    cases are to be added;
  - regression of the non-GPU path.
- Hardware checks (the gate):
  - `nvidia-peermem` loads on the target kernel and network driver stack, and a GPU buffer
    registers;
  - a GPU update to a replicated object succeeds on verbs and on UCX;
  - repeated block I/O on one handle keeps a single registration per context (read from the
    libfabric or UCX cache statistics), in-use entries are not evicted, and `cudaFree()` and
    reuse of an address are caught;
  - the largest region that registers; a region too large falls back to per-I/O registration;
  - I/O and `daos_mem_bind()` from a thread without a CUDA context, with the cache on and off;
  - why the MR cache is off for verbs clients, before `CRT_MRC_ENABLE=1` is recommended.
- Error injection: bulk creation and registration failures in a multi-sgl I/O.
- Performance against the host bounce-buffer path for a range of sizes, and one event queue
  against 2 and 4 per process, before claiming any gain. The external test measured GPU reads
  at 0.86 to 1.14 times the host path on verbs and about half of it on UCX, with about 8 times
  less host memory traffic; the gain to expect is host memory bandwidth, not throughput.

## Risks, Mitigations and Future Works

### Risks and Mitigations

- **Silent double registration**: a cache miss on a registered region is invisible at default
  log levels. Mitigation: client counters, in a follow-up change.
- **Cache behavior is not a contract**, and the cache is off by default on verbs clients.
  Mitigation: the hardware gate; alternative A.4 as fallback.
- **No end-to-end integrity**: GPU I/O skips client checksums, and GPU fetches from checksum
  containers return unverified data.
- **Checksums and EC by default**: GPU updates fail on a default container, and GPU I/O
  fails on a default DFS file in a redundant container (see
  [Client-Side Safety Checks](#client-side-safety-checks) and
  [DFS and Array APIs](#dfs-and-array-apis)). Mitigation: `cksum:off` and an explicit object
  class; phase 4 for EC; user-supplied or server-side checksums later.
- **Replicated objects**: the external test reports that a GPU update
  to a replicated object fails on verbs (the server's RDMA read times out), while
  non-replicated objects and UCX work. Until it is reproduced and fixed, a GPU write may have
  no redundancy at all, since EC is rejected. Mitigation: reproduce it first; it is part of
  the hardware gate.
- **Lifetimes**: GPU buffers must stay valid until the I/O completes, and a region must be
  deregistered before `cudaFree()`. An I/O cannot be aborted (see the rules in
  [Public APIs](#public-apis)), so a caller that gives up on a slow I/O and reuses its block
  reads or stores wrong data. While an engine is down the block stays in use and
  `daos_mem_deregister()` returns `-DER_BUSY`; that holds a block, it does not corrupt data.
  Mitigation: consumers with a deadline keep such blocks out of their free list.
- **Driver and NIC support**: registration of CUDA memory fails without `nvidia-peermem` (see
  [GPU Vendors](#gpu-vendors)). Per-I/O failures are returned; a failed
  region falls back to per-I/O registration.

### Future Works

- cuFile and cuObject, by one of the client-only routes on the current APIs, once one is
  verified on a GPU node. See [integrations.md](integrations.md#cufile-and-cuobject).
- Pin a region once per process, by one of two ways. Preferred: a network domain shared by all
  contexts (several Mercury endpoints on one `fi_domain`, or UCX workers on one
  `ucp_context`). Every lane keeps its own endpoint, completion queue and progress; it needs
  Mercury upstream. The other: event queues sharing a CaRT context (an option of
  `daos_eq_create()`). It needs DAOS only, but the lanes then share one completion queue and
  its progress, so it is a stopgap. `daos_mem_bind()` is the same call in both.
- GPU/NIC topology-aware interface selection, from the device id.
- A first run on hardware for each vendor, tests for ROCm and Level Zero, and a build option
  for Level Zero (see [GPU Vendors](#gpu-vendors)).
- User-supplied data checksums in `ma_csums`: the caller computes them on the GPU for updates and
  gets the stored ones back on fetch, with a helper for the container's chunk layout.
- Server-side checksums for GPU updates (protects data at rest only).
- Server-side EC for GPU I/O beyond arrays: degraded reads and large single values
  ([server_ec.md](server_ec.md), phases 2 and 3).
- Holes: for a GPU fetch the server could send zeros instead of skipping the hole, which removes
  the hole restriction of the DFS and array calls. A server change; not designed yet.
- A real I/O abort, if DAOS gets one, has to cover registered regions: the registration of a
  region outlives the aborted I/O, so nothing stops a late transfer by the server. It needs
  the server to fence the transfer, or the client to hold the block until the server is done.

## Alternatives Considered

| | Alternative | Why not |
|-|-------------|---------|
| A.1 | Memory type in `d_iov_t` | ABI and wire change for every user |
| A.2 | Per-lane slices of a region | blocks across slices lose the handle, unbalanced lanes |
| A.3 | Mercury view handle on the pool key | wrong address without `FI_MR_VIRT_ADDR` |
| A.4 | Block offset in `obj_rw_in` | RPC version bump, CPD changes; kept as fallback |
| A.5 | Server-side region table | per-client server state, cleanup, wire change |
| A.6 | CaRT SEP mode for a shared domain | Mercury SEP not on verbs, tcp, cxi or UCX |
| A.7 | Import an RDMA key registered by another library | invalid outside its domain, see below |

### A.7 Import an RDMA Key Registered by Another Library

The prototype added a key to `daos_mem_attr_t` (`ma_rkey`), a CaRT call and a Mercury patch
that put it into the bulk handle in place of a registration by DAOS. The aim was to reuse a
registration that someone else already holds (libcufile for a GPU buffer, or the transfer
library of a framework for its host pool) and so avoid both the registration per I/O and a
second pin of the same memory. It is not supported, for these reasons:

- **A key is only valid in the protection domain that registered it.** The NIC checks an
  incoming RDMA request against the protection domain of the connection it arrives on. DAOS
  connections belong to the domain of its libfabric or UCX instance; libcufile, a framework's
  transfer engine or another UCX instance register in their own. Neither libfabric nor UCX can
  join a domain created elsewhere, so a server that uses the imported key on a DAOS connection
  gets a remote access error. This holds for host memory as much as for GPU memory.
- **The cuFile descriptor is not a key.** It describes the buffer for a transfer that the
  storage server starts on a DC (dynamically connected) connection of its own; NVIDIA
  documents that the cuObject callback must hand it to NVIDIA's server library. The prototype
  read its first 8 bytes as a key.
- **No transport support.** UCX cannot import a key at all, and on libfabric it needed the
  Mercury patch, which is not upstream.
- **No safety.** A raw key carries no bounds, so DAOS cannot check that the iovs lie inside
  the registered memory, and the registration can be released by its owner while a DAOS
  transfer is running.
- **No precedent.** No public file system was found that imports the key: they register the
  GPU memory themselves, or run the DC transfer on the server.

What serves the need instead is `daos_mem_register()`: DAOS registers the region once in its
own domain, with bounds and a reference per I/O. The price is a second pin of memory that
another library also registered, which counts against the GPU BAR and the NIC limits. A
server-side DC path, the only way to really use the cuFile descriptor, is a Non-Goal here
(see [integrations.md](integrations.md#cufile-and-cuobject)).

## References

- [GPUDirect RDMA](https://docs.nvidia.com/cuda/gpudirect-rdma/)
- [NVIDIA GDS overview](https://docs.nvidia.com/gpudirect-storage/overview-guide/)
- [NVIDIA cuObject](https://docs.nvidia.com/gpudirect-storage/cuobject/index.html)
- [libfabric fi_mr (FI_HMEM)](https://ofiwg.github.io/libfabric/main/man/fi_mr.3.html)
- [Mercury](https://mercury-hpc.github.io/)
- [NVIDIA NIXL](https://github.com/ai-dynamo/nixl),
  [Dynamo](https://github.com/ai-dynamo/dynamo), [LMCache](https://github.com/LMCache/LMCache),
  [Mooncake](https://github.com/kvcache-ai/Mooncake)
- [Prototype](https://github.com/daos-stack/daos/pull/18566) and its review comments
- [libfabric dma-buf fix](https://github.com/ofiwg/libfabric/pull/12828)
- External test: [gluesys/lmcache-daos](https://github.com/gluesys/lmcache-daos)
  (`gpudirect/README.md`, `doc/NIXL-DAOS-VRAM.md`, `doc/FAILURE-MODES.md`,
  `doc/upstream/daos-gds-replicated-write-report.md`)
- NIXL backends for DAOS: [on libdfs](https://github.com/ai-dynamo/nixl/pull/2261),
  [on the object API](https://github.com/gluesys/nixl-daos)
