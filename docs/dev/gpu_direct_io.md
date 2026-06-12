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

1. **No wire protocol changes** — Rolling upgrades must work. GPU memory
   metadata is local-only and never serialized on the wire.
2. **Backward compatible** — Existing `daos_obj_fetch()`/`daos_obj_update()`
   APIs are unchanged. New GPU-aware wrappers are provided.
3. **Transport agnostic** — Works with both libfabric (OFI) and UCX, as long as
   the provider supports `FI_HMEM` (OFI) or memory type registration (UCX).
4. **Opt-in at build time and runtime** — `BUILD_GPU_DIRECT=yes` SCons option
   enables GPU support in dependencies. `D_GPU_DIRECT=1` env var activates at
   runtime.
5. **CUDA first, extensible** — Initial implementation targets NVIDIA GPUs via
   CUDA. Enum types defined for ROCm and Level Zero (Intel) for future use.

## Architecture

### Component Diagram

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

## API Design

### Side-Channel Approach

The key design decision is using a **side-channel** (`daos_mem_attr_t`) rather
than extending `d_iov_t`. This avoids:

- Wire protocol changes (breaking rolling upgrades)
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

### New Public APIs

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

## Data Flow

### Write Path (daos_obj_update_gpu)

```
1. Application calls daos_obj_update_gpu(sgls, mem_attrs)
2. Client validates: mem_attrs != NULL, mem_type != HOST, D_GPU_DIRECT enabled
3. Client sets ORF_GPU_DIRECT in RPC flags (for server observability)
4. Client calls crt_bulk_create_with_mem_attr(sgl, mem_attr)
5. CaRT forwards mem_type to Mercury: HG_Bulk_create() with CUDA attribute
6. Mercury/libfabric registers GPU memory via nvidia-peermem for RDMA
7. RPC sent to server with bulk handle (handle is opaque — no wire change)
8. Server does HG_Bulk_transfer() — RDMA pulls directly from GPU memory
9. Server writes to VOS/BIO as normal (data is now in server memory)
```

### Read Path (daos_obj_fetch_gpu)

```
1. Application calls daos_obj_fetch_gpu(sgls, mem_attrs)
2. Client creates bulk handle with GPU memory attributes
3. RPC sent to server
4. Server does HG_Bulk_transfer() — RDMA pushes directly into GPU memory
5. Transfer complete, application's GPU buffers now contain the data
```

## Build System

### SCons Options

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

### Runtime Configuration

| Environment Variable | Default | Description |
|---------------------|---------|-------------|
| `D_GPU_DIRECT` | `0` | Set to `1` to enable GPU direct path at runtime |

## Alternatives Considered

### 1. Extend `d_iov_t` with Memory Type Field

**Rejected** — Would change the struct layout, breaking ABI compatibility and
requiring wire protocol versioning for rolling upgrades. Every component touching
`d_iov_t` would need updates.

### 2. NVIDIA GDS (cuFile) Kernel Plugin

**Not viable** — GDS filesystem integration requires a kernel module implementing
`struct nvfs_dma_rw_ops` and registering with `nvidia_fs.ko`. DAOS is a
userspace storage system with no kernel filesystem module. The GDS plugin API is:
- Kernel-only (not userspace)
- Not publicly documented for third parties
- Requires NDA for non-standard filesystems

See: https://github.com/NVIDIA/gds-nvidia-fs (`src/nvfs-dma.h`)

### 3. Bounce Buffer in libdaos

**Baseline (current behavior)** — Applications today must `cudaMemcpy()` to host
memory before calling DAOS APIs. This works but adds latency and memory pressure.
Our implementation eliminates this for GPU-aware deployments.

## Dependencies

| Component | Version | Purpose |
|-----------|---------|---------|
| CUDA Toolkit | ≥ 11.0 | `cuda.h`, `cuda_runtime.h` for memory type detection |
| GDRCopy | ≥ 2.3 | Low-latency GPU memory copy for small transfers |
| nvidia-peermem | (kernel module) | Enables RDMA adapters to access GPU memory |
| libfabric | ≥ 1.15 | `FI_HMEM` support for GPU memory registration |
| Mercury | ≥ 2.3 | `HG_Bulk_create_attr()` with memory type (may need upstream patch) |

## Testing Strategy

### Unit Tests
- `crt_bulk_create_with_mem_attr()` with NULL mem_attr → same as `crt_bulk_create()`
- `daos_obj_fetch_gpu()` with `D_GPU_DIRECT=0` → returns `-DER_NOSYS`
- `daos_obj_update_gpu()` with invalid `mem_type` → returns `-DER_INVAL`
- OBJ RPC flag propagation (ORF_GPU_DIRECT set correctly)

### Integration Tests (require GPU hardware)
- Round-trip: `daos_obj_update_gpu()` → `daos_obj_fetch_gpu()` → verify GPU buffer
- Mixed: update from GPU, fetch to host memory (and vice versa)
- Large transfers: multi-MB SGLs with multiple GPU-resident iovs
- Error handling: GPU buffer freed before transfer completes

### Performance Tests
- Bandwidth comparison: GPU direct vs bounce buffer for 1MB–1GB transfers
- Latency comparison: small (4KB–64KB) GPU direct vs bounce buffer
- Multi-GPU: concurrent transfers from different GPU devices

## Future Work

1. **ROCm / Level Zero support** — Enum types defined; implementation follows
   same pattern with `hip*`/`ze*` APIs
2. **Array API** — `daos_array_read_gpu()`/`daos_array_write_gpu()` wrappers
3. **DFS (POSIX) integration** — GPU-aware `dfs_read()`/`dfs_write()` for
   applications using the POSIX interface
4. **UCX transport** — Verify `ucp_mem_map()` with `UCS_MEMORY_TYPE_CUDA`
5. **Managed memory optimization** — For `CUDA_MANAGED` type, potentially skip
   RDMA registration if page is already host-resident
6. **DFUSE + GDS** — Long-term investigation into whether a FUSE-bypass
   mechanism could enable GDS kernel path (highly speculative)

## References

- [GPUDirect RDMA Documentation](https://docs.nvidia.com/cuda/gpudirect-rdma/)
- [NVIDIA GDS Architecture](https://docs.nvidia.com/gpudirect-storage/overview-guide/)
- [nvidia-fs kernel module source](https://github.com/NVIDIA/gds-nvidia-fs)
- [libfabric FI_HMEM](https://ofiwg.github.io/libfabric/main/man/fi_mr.3.html)
- [Mercury Heterogeneous Memory](https://mercury-hpc.github.io/)
- [GDRCopy](https://github.com/NVIDIA/gdrcopy)
