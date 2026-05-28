# GPU Direct I/O Support for DAOS

## Status

**Draft**

## Overview

This document describes the design and implementation of GPU direct I/O support
in DAOS. The feature enables RDMA transfers directly between GPU memory and DAOS
storage targets, eliminating the need to stage data through host memory (bounce
buffers).

## Motivation

GPU-intensive workloads (AI/ML training, HPC simulations, scientific computing)
frequently need to persist large tensors, checkpoints, or intermediate results.
Without GPU direct I/O, the data path is:

```
GPU Memory → cudaMemcpy → Host Buffer → DAOS Client → Network → DAOS Server
```

With GPU direct I/O via GPUDirect RDMA:

```
GPU Memory → RDMA (network) → DAOS Server
```

This eliminates one full memory copy and reduces latency by ~50% for large
transfers.

## Design Principles

1. **No wire protocol changes** — No change to data structre used by wire-protocol.
2. **Backward compatible** — Existing `daos_obj_fetch()`/`daos_obj_update()`
   APIs are unchanged. New GPU-aware wrappers are provided.
3. **Transport agnostic** — Works with both libfabric (OFI) and UCX, as long as
   the provider supports `FI_HMEM` (OFI) or memory type registration (UCX).
4. **Opt-in at build time** — `BUILD_GPU_DIRECT=yes` SCons option enables GPU
   support in dependencies. Runtime activation is automatic via cuFile.
5. **CUDA first, extensible** — Initial implementation targets NVIDIA GPUs via
   CUDA. Enum types defined for ROCm and Level Zero (Intel) for future use.

## Architecture

### Data Flow

```
┌─────────────────────────────────────────────────────────────┐
│  Application                                                 │
│  ┌─────────────────────────────────────────────────────────┐│
│  │ daos_obj_fetch_gpu() / daos_obj_update_gpu()            ││
│  │ + daos_mem_attr_t (side-channel)                        ││
│  └──────────────────────────┬──────────────────────────────┘│
└─────────────────────────────┼───────────────────────────────┘
                              │
┌─────────────────────────────▼───────────────────────────────┐
│  DAOS Client (libdaos)                                       │
│  ┌─────────────────────────────────────────────────────────┐│
│  │ obj_bulk_prep() — validates GPU buffers, sets ORF flag  ││
│  │ crt_bulk_create_with_mem_attr() — passes mem type       ││
│  └──────────────────────────┬──────────────────────────────┘│
└─────────────────────────────┼───────────────────────────────┘
                              │
┌─────────────────────────────▼───────────────────────────────┐
│  CaRT Transport Layer                                        │
│  ┌─────────────────────────────────────────────────────────┐│
│  │ crt_bulk_create_with_mem_attr()                         ││
│  │   → HG_Bulk_create() with mem_type attribute            ││
│  │   → Mercury registers GPU memory for RDMA               ││
│  └──────────────────────────┬──────────────────────────────┘│
└─────────────────────────────┼───────────────────────────────┘
                              │
┌─────────────────────────────▼───────────────────────────────┐
│  Mercury / libfabric (or UCX)                                │
│  ┌─────────────────────────────────────────────────────────┐│
│  │ FI_HMEM memory registration (CUDA, ROCm, ZE)           ││
│  │ GPUDirect RDMA via nvidia-peermem / gdrcopy             ││
│  └─────────────────────────────────────────────────────────┘│
└─────────────────────────────────────────────────────────────┘
                              │
                    ┌─────────▼─────────┐
                    │  DAOS Server      │
                    │  (unchanged —     │
                    │   RDMA is         │
                    │   transparent)    │
                    └───────────────────┘
```

---

## Object API Extension

### Side-Channel Approach

The key design decision is using a **side-channel** (`daos_mem_attr_t`) rather
than extending `d_iov_t`. This avoids:

- Wire protocol changes
- ABI changes to the fundamental scatter-gather type
- Any impact on non-GPU I/O paths

```c
/** Memory type for heterogeneous memory support */
typedef enum {
    DAOS_MEM_TYPE_HOST       = 0, /**< Regular host/CPU memory */
    DAOS_MEM_TYPE_CUDA       = 1, /**< NVIDIA CUDA device memory */
    DAOS_MEM_TYPE_CUDA_MANAGED = 2, /**< NVIDIA CUDA managed/unified memory */
    DAOS_MEM_TYPE_ROCM       = 3, /**< AMD ROCm device memory */
    DAOS_MEM_TYPE_ZE         = 4, /**< Intel Level Zero device memory */
} daos_mem_type_t;

/** Memory attributes for GPU direct I/O (side-channel, never on wire) */
typedef struct {
    daos_mem_type_t  ma_mem_type;   /**< Memory type of the buffers */
    int              ma_device_id;  /**< Device ordinal (e.g., CUDA device 0) */
} daos_mem_attr_t;
```

### Public APIs

```c
/**
 * Fetch object data into GPU memory buffers.
 * Same semantics as daos_obj_fetch() but with GPU memory support.
 */
int daos_obj_fetch_gpu(daos_handle_t oh, daos_handle_t th, uint64_t flags,
                       daos_key_t *dkey, unsigned int nr, daos_iod_t *iods,
                       d_sg_list_t *sgls, daos_mem_attr_t *mem_attrs,
                       daos_iom_t *ioms, daos_event_t *ev);

/**
 * Update object with data from GPU memory buffers.
 * Same semantics as daos_obj_update() but with GPU memory support.
 */
int daos_obj_update_gpu(daos_handle_t oh, daos_handle_t th, uint64_t flags,
                        daos_key_t *dkey, unsigned int nr, daos_iod_t *iods,
                        d_sg_list_t *sgls, daos_mem_attr_t *mem_attrs,
                        daos_event_t *ev);
```

### CaRT Bulk API Extension

```c
/**
 * Create a bulk handle with memory type attributes.
 * Original crt_bulk_create() remains unchanged (calls this with NULL).
 */
int crt_bulk_create_with_mem_attr(crt_context_t crt_ctx,
                                  d_sg_list_t *sgl,
                                  crt_bulk_perm_t bulk_perm,
                                  daos_mem_attr_t *mem_attr,
                                  crt_bulk_t *bulk_hdl);
```

### Client-Side Safety Checks

When `DAOS_OBJ_IO_GPU_DIRECT` flag is set, the client enforces:

1. **Force bulk transfer** — Small GPU I/O that would normally be inlined in the
   RPC is forced to use bulk transfer. Inlining would `memcpy()` GPU pointers
   during RPC encoding → segfault.

2. **Skip checksum** — Client-side checksum computation reads buffer content,
   which segfaults on GPU pointers. Checksums are skipped for GPU direct I/O.

3. **Reject EC objects** — EC encode/decode requires reading buffer content on
   the CPU for parity computation. Returns `-DER_NOTSUPPORTED` for EC objects.

### Write Path (daos_obj_update_gpu)

```
1. Application calls daos_obj_update_gpu(sgls, mem_attrs)
2. Client validates: mem_attrs != NULL, mem_type != HOST, object is not EC
3. Client sets ORF_GPU_DIRECT in RPC flags (for server observability)
4. Client skips checksum computation (GPU buffers not host-readable)
5. Client forces bulk transfer (no inline packing of GPU pointers)
6. Client calls crt_bulk_create_with_mem_attr(sgl, mem_attr)
7. CaRT forwards mem_type to Mercury: HG_Bulk_create() with CUDA attribute
8. Mercury/libfabric registers GPU memory via nvidia-peermem for RDMA
9. RPC sent to server with bulk handle (handle is opaque — no wire change)
10. Server does HG_Bulk_transfer() — RDMA pulls directly from GPU memory
11. Server writes to VOS/BIO as normal (data is now in server memory)
```

### Read Path (daos_obj_fetch_gpu)

```
1. Application calls daos_obj_fetch_gpu(sgls, mem_attrs)
2. Client validates: object is not EC, mem_attrs valid
3. Client creates bulk handle with GPU memory attributes
4. RPC sent to server
5. Server does HG_Bulk_transfer() — RDMA pushes directly into GPU memory
6. Transfer complete, application's GPU buffers now contain the data
```

---

## Build System

```bash
# Enable GPU direct support (adds CUDA/GDRCopy to dependency build)
scons BUILD_GPU_DIRECT=yes install

# Without GPU support (default — no CUDA dependencies required)
scons install
```

When `BUILD_GPU_DIRECT=yes`:
- libfabric built with `--enable-hook_hmem --with-cuda=/usr/local/cuda`
- Mercury built with `-DNA_OFI_GDR=ON`
- GDRCopy headers/libs expected at system paths

## Dependencies

| Component | Version | Purpose |
|-----------|---------|---------|
| CUDA Toolkit | ≥ 12.2 | `cuda.h`, `cuda_runtime.h` |
| GDRCopy | ≥ 2.3 | Low-latency GPU memory copy for small transfers |
| nvidia-peermem | (kernel module) | GPU RDMA |
| libfabric | ≥ 1.15 | `FI_HMEM` support for GPU memory registration |
| Mercury | ≥ 2.3 | `HG_Bulk_create_attr()` with memory type |

## References

- [GPUDirect RDMA Documentation](https://docs.nvidia.com/cuda/gpudirect-rdma/)
- [NVIDIA GDS Architecture](https://docs.nvidia.com/gpudirect-storage/overview-guide/)
- [libfabric FI_HMEM](https://ofiwg.github.io/libfabric/main/man/fi_mr.3.html)
- [Mercury Heterogeneous Memory](https://mercury-hpc.github.io/)
- [GDRCopy](https://github.com/NVIDIA/gdrcopy)
