# DAOS cuFile and cuObject Plugins

## Status

**Draft**

## Overview

This document describes the DAOS cuFile userspace filesystem plugin and the
planned cuObject plugin. Both integrate DAOS with NVIDIA's GPUDirect Storage
ecosystem, enabling AI/ML frameworks to use DAOS for GPU direct I/O with
minimal or zero application changes.

These plugins build on the GPU direct I/O infrastructure documented in
[daos_gpu_direct.md](daos_gpu_direct.md).

---

## cuFile Userspace FS Plugin

The cuFile API (part of GDS, CUDA Toolkit 12.2+) provides a **userspace
filesystem plugin interface** via `CU_FILE_HANDLE_TYPE_USERSPACE_FS`. This
allows any userspace storage system to plug into the standard cuFile APIs
(`cuFileRead`/`cuFileWrite`) without a kernel module.

This is the **highest-value integration path** for the AI/ML ecosystem, because
frameworks like PyTorch DataLoader, NVIDIA DALI, and KvikIO already use cuFile
APIs. A DAOS cuFile plugin makes these frameworks work with DAOS transparently.

### cuFile Userspace FS API

```c
/* From cuFile API (CUDA Toolkit) */
enum CUfileFileHandleType {
    CU_FILE_HANDLE_TYPE_OPAQUE_FD    = 1,  /* Linux fd (kernel GDS) */
    CU_FILE_HANDLE_TYPE_OPAQUE_WIN32 = 2,  /* Windows handle */
    CU_FILE_HANDLE_TYPE_USERSPACE_FS = 3,  /* Userspace FS plugin ← */
};

typedef struct CUfileFSOps {
    /* Return filesystem type name (e.g., "DAOS") */
    const char* (*fs_type)(void *handle);

    /* List of RDMA device addresses to use (NULL = no restriction) */
    int (*getRDMADeviceList)(void *handle, sockaddr_t **hostaddrs);

    /* RDMA device priority for this I/O (-1 = no preference) */
    int (*getRDMADevicePriority)(void *handle, char*, size_t,
                                 loff_t, sockaddr_t* hostaddr);

    /* Read: fill buffer from storage, with optional RDMA token */
    ssize_t (*read)(void *handle, char*, size_t, loff_t, cufileRDMAInfo_t*);

    /* Write: store buffer to storage, with optional RDMA token */
    ssize_t (*write)(void *handle, const char*, size_t, loff_t, cufileRDMAInfo_t*);
} CUfileFSOps_t;

typedef struct CUfileDescr_t {
    CUfileFileHandleType type;
    union { int fd; void *handle; } handle;
    const CUfileFSOps_t *fs_ops;  /* FS operation table */
} CUfileDescr_t;
```

### Plugin Design

The plugin is a shared library (`libdaos_cufile.so`) that implements
`CUfileFSOps_t` using DFS (DAOS File System) as the backend. Key internals:

```c
/* Internal mount — refcounted, cached per pool+container */
struct daos_cufile_mount {
    dfs_t           *dfs;         /* DFS filesystem handle */
    daos_handle_t    poh;         /* pool handle */
    daos_handle_t    coh;         /* container handle */
    char            *pool_label;  /* for reconnection */
    char            *cont_label;
    pthread_mutex_t  mount_lock;  /* protects reconnection */
    bool             connected;
};

/* Internal file handle — one per open file */
struct daos_cufile_handle {
    daos_cufile_mount_t *mount;
    dfs_obj_t           *obj;       /* DFS file object */
    pthread_mutex_t      lock;      /* thread safety for concurrent I/O */
    int                  flags;     /* open flags */
    char                *path;      /* stored for reconnection */
    bool                 auto_mount;/* true if lazily connected */
};

/* Read callback — implements the CUfileFSOps_t read interface */
static ssize_t daos_cufile_read(void *handle, char *buf, size_t size,
                                loff_t offset, cufileRDMAInfo_t *rdma_info) {
    struct daos_cufile_handle *dfh = handle;

    if (has_gpu_rdma_info(rdma_info)) {
        /* GPU direct: RDMA via dfs_read_gpu() + CaRT HMEM */
        ret = gpu_direct_read(dfh, buf, size, offset, rdma_info);
        if (ret < 0 && try_reconnect(dfh, -ret))
            ret = gpu_direct_read(dfh, buf, size, offset, rdma_info);
    } else {
        ...
    }
    ...
}

/* gpu_direct_read — thin wrapper that sets up mem_attr and calls dfs_read_gpu */
static ssize_t gpu_direct_read(struct daos_cufile_handle *dfh, char *buf,
                               size_t size, daos_off_t offset,
                               cufileRDMAInfo_t *rdma_info) {
    daos_mem_attr_t mem_attr = { .ma_mem_type = DAOS_MEM_TYPE_CUDA };

    /* Import pre-registered RDMA key (avoids double NIC registration) */
    if (has_gpu_rdma_info(rdma_info))
        d_iov_set(&mem_attr.ma_rkey, rdma_info->desc_str, rdma_info->desc_len);

    d_iov_set(&iov, buf, size);
    sgl = { .sg_nr = 1, .sg_iovs = &iov };

    rc = dfs_read_gpu(dfh->mount->dfs, dfh->obj, &sgl, offset, &read_size, &mem_attr);
    return rc ? -rc : (ssize_t)read_size;
}
```

### Application Usage

The plugin supports three usage modes, from most transparent to most explicit:

#### Mode 1: dfuse + fd-based (Recommended — Near-Zero App Changes)

This mode uses dfuse purely for **file namespace discovery** — the app calls
`open()` on a dfuse mount to get an fd, which the plugin uses as an opaque
token to discover the DAOS pool, container, and file path. All data I/O
bypasses FUSE entirely and goes directly to DAOS servers via DFS.

```c
#include <cufile.h>
#include <daos_cufile.h>  /* provides daos_cufile_ops symbol */

/* dfuse provides file namespace — same as any POSIX filesystem */
int fd = open("/mnt/daos/data/model.pt", O_RDONLY);

/* Register with cuFile — only 2 lines differ from kernel GDS: */
CUfileDescr_t desc = {0};
desc.type   = CU_FILE_HANDLE_TYPE_USERSPACE_FS;  /* changed from OPAQUE_FD */
desc.handle.fd = fd;                              /* same fd from dfuse */
desc.fs_ops = &daos_cufile_ops;                   /* added: DAOS plugin ops */

CUfileHandle_t cfh;
cuFileHandleRegister(&cfh, &desc);

/* Standard cuFile GPU direct I/O — identical to Lustre/WekaFS usage */
void *gpu_buf;
cudaMalloc(&gpu_buf, 1UL << 30);
cuFileBufRegister(gpu_buf, 1UL << 30, 0);

ssize_t ret = cuFileRead(cfh, gpu_buf, 1UL << 30, 0, 0);

cuFileBufDeregister(gpu_buf);
cuFileHandleDeregister(cfh);
cudaFree(gpu_buf);
close(fd);
```

Build: `gcc app.c -lcufile -ldaos_cufile -lcuda -o app`

**Role of the fd:** cuFile does NOT use the fd for any I/O on USERSPACE_FS
handles — it simply passes `desc.handle.fd` back to our callbacks as the
`void *handle` parameter (cast from int). Our plugin uses this fd solely to:
1. `ioctl(fd, DFUSE_IOCTL_IL_SIZE)` → get serialized pool/container/DFS handle sizes
2. `ioctl(fd, DFUSE_IOCTL_IL_DSIZE)` → get serialized DFS object handle size
3. Fetch serialized pool/cont/DFS/object handles via further ioctls
4. Import handles via `daos_pool_global2local()`, `dfs_global2local()`, etc.

After this one-time resolution (on first I/O), the fd is never used again.
The plugin caches the resolved DFS handle and all subsequent I/O goes
directly to DAOS servers without touching dfuse or the kernel.

**Why dfuse is still needed:** The application needs a POSIX path to `open()`.
Without dfuse, the app must know pool/container labels explicitly (Mode 2).
dfuse provides the familiar filesystem namespace so the app code looks
identical to Lustre/WekaFS — just `open("/mnt/storage/file")`.

**Error handling:** Mode 1 does NOT attempt reconnection on I/O errors. Since
dfuse itself does not reconnect after pool eviction, reconnecting the plugin's
DFS handle alone would be inconsistent. Errors are returned directly to cuFile.

**Comparison with Lustre/WekaFS (kernel GDS):**

| | Lustre/WekaFS | DAOS (this mode) |
|---|---|---|
| `open()` | Same | Same (dfuse) |
| `descr.type` | `OPAQUE_FD` | `USERSPACE_FS` |
| `descr.fs_ops` | Not set | `&daos_cufile_ops` |
| Kernel module | nvidia-fs.ko + FS driver | Not needed |
| Data path | DMA via kernel | Direct DFS (userspace) |
| Link against | `-lcufile` | `-lcufile -ldaos_cufile` |

#### Mode 2: Explicit DAOS connection (No dfuse Required)

For environments without dfuse, or when the application already manages DAOS
connections. Uses `daos_cufile_register()` which handles pool/container
connection internally. No dfuse mount or fd required:

```c
#include <cufile.h>
#include <daos_cufile.h>

/* Single call sets up everything — pool/cont from env vars or explicit args */
CUfileHandle_t     cfh;
daos_cufile_reg_t *reg;

/* Option A: Use DAOS_POOL/DAOS_CONT environment variables */
daos_cufile_register("/data/model.pt", O_RDONLY, NULL, NULL, &cfh, &reg);

/* Option B: Specify pool/container explicitly */
daos_cufile_register("/data/model.pt", O_RDONLY, "mypool", "mycont", &cfh, &reg);

/* Register with cuFile using the prepared descriptor */
cuFileHandleRegister(&cfh, daos_cufile_get_desc(reg));

/* Standard cuFile GPU direct I/O — identical to Mode 1 from here */
void *gpu_buf;
cudaMalloc(&gpu_buf, 1UL << 30);
cuFileBufRegister(gpu_buf, 1UL << 30, 0);
ssize_t ret = cuFileRead(cfh, gpu_buf, 1UL << 30, 0, 0);

cuFileBufDeregister(gpu_buf);
cuFileHandleDeregister(cfh);
cudaFree(gpu_buf);
daos_cufile_deregister(reg);  /* closes file + disconnects */
```

**Key difference from Mode 1:** No dfuse or fd needed. The plugin connects
to DAOS directly and passes a `daos_cufile_handle_t*` pointer to cuFile
(not an fd). Mode 2 supports automatic reconnection on pool eviction.

**Mode 1 vs Mode 2 summary:**

| | Mode 1 (fd-based) | Mode 2 (explicit) |
|---|---|---|
| Requires dfuse | Yes | No |
| App knows pool/cont | No (auto-discovered) | Yes (env vars or explicit) |
| Public API used | `daos_cufile_ops` | `daos_cufile_register/deregister` |
| `desc.handle` | fd (int, opaque token) | daos_cufile_handle_t* (pointer) |
| First I/O | Lazy: resolve fd → dfs_connect | Immediate: connected at register |
| Reconnect on error | No (dfuse doesn't reconnect) | Yes (auto-retry) |
| Code changes vs Lustre | 2 lines | DAOS-specific API calls |

#### Mode 3: IL interception (Zero App Changes — Future)

For maximum transparency, a cuFile interception library (`libdaos_cufile_il.so`)
can be LD_PRELOADed. It intercepts `cuFileHandleRegister` and, when the fd
belongs to a dfuse mount, rewrites the descriptor from `OPAQUE_FD` to
`USERSPACE_FS` with DAOS plugin ops. The application requires zero code changes:

```bash
# App binary is completely unmodified — identical to Lustre usage
LD_PRELOAD=libdaos_cufile_il.so ./my_gds_app /mnt/daos/data/model.pt
```

This intercepts only `cuFileHandleRegister` and `cuFileHandleDeregister` (2
symbols). Status: planned for a future phase.

### Advantages

1. **Near-zero application changes** — Mode 1 requires only 2-line change from
   the standard kernel GDS pattern; Mode 3 requires zero changes
2. **No kernel module needed** — Pure userspace via `CU_FILE_HANDLE_TYPE_USERSPACE_FS`;
   no nvidia-fs.ko integration required
3. **Compatibility mode built-in** — cuFile falls back to bounce buffers on
   non-RDMA hardware, so the plugin works everywhere
4. **Leverages existing DFS** — Plugin uses `dfs_read_gpu()`/`dfs_write_gpu()` for GPU direct
   and `dfs_read()`/`dfs_write()` for host buffer path
5. **No server changes needed** — Both host buffer and GPU direct paths use existing
   DAOS infrastructure (DFS and CaRT HMEM bulk)
6. **Same UX as kernel GDS filesystems** — Mode 1 mirrors the Lustre/WekaFS/GPFS
   pattern: `open()` + `cuFileHandleRegister` + `cuFileRead/Write`

### I/O Data Path

The plugin's read/write callbacks receive two parameters relevant to data transfer:
- **`buf`** — Always host-accessible memory (bounce buffer or BAR1-mapped GPU view)
- **`rdma_info`** — When non-NULL, contains GPU RDMA descriptors for zero-copy

Two I/O paths based on `rdma_info`:

```
rdma_info != NULL (GPU direct):
  cuFile → callback(buf, rdma_info) → dfs_read_gpu/write_gpu() → CaRT HMEM RDMA → server

rdma_info == NULL (host buffer / compat mode):
  cuFile → callback(buf, NULL) → dfs_read/write(buf) → DAOS
```

**GPU direct path** (`rdma_info` present): Uses `dfs_read_gpu()` /
`dfs_write_gpu()` which handle DFS array layout (chunk splitting) internally
and call `daos_obj_fetch_gpu()`/`daos_obj_update_gpu()` with
`DAOS_MEM_TYPE_CUDA` via CaRT HMEM bulk transfer. The pre-registered RDMA key
from `rdma_info->desc_str` is passed via `mem_attr.ma_rkey` to avoid double
NIC registration (works on OFI; UCX falls back to native HMEM registration).

**Host buffer path** (`rdma_info` absent): Performs standard `dfs_read()` /
`dfs_write()` on the host-accessible `buf`. This is the compatibility path
that works on all hardware.

### Implementation Status

The plugin is implemented at `src/client/cufile/`:

| File | Purpose |
|------|---------|
| `src/include/daos_cufile.h` | Public API header |
| `src/include/daos_fs.h` | `dfs_read_gpu()`/`dfs_write_gpu()` declarations |
| `src/client/dfs/io.c` | `dfs_read_gpu()`/`dfs_write_gpu()` implementations |
| `src/include/cart/api.h` | `crt_bulk_import_rkey()` declaration |
| `src/cart/crt_bulk.c` | `crt_bulk_import_rkey()` + fallback logic |
| `deps/patches/mercury/0004_import_rkey.patch` | Mercury patch for NA/HG rkey import |
| `cufile_internal.h` | Internal shared structs and declarations |
| `cufile_plugin.c` | Core plugin: callbacks, ops table, fd resolution, mount cache |
| `cufile_daos.c` | Mode 2 API: connect/disconnect/open/close + transparent register |
| `tests/daos_cufile_example.c` | Mode 1 example (dfuse + fd-based) |
| `tests/daos_cufile_mode2_example.c` | Mode 2 example (explicit DAOS, no dfuse) |
| `tests/SConscript` | Build test programs |
| `SConscript` | Build as `libdaos_cufile.so` |
| `README.md` | Usage documentation |

#### Implementation Phases

| Phase | Scope | GPU Direct? | Status |
|-------|-------|-------------|--------|
| Phase 1 | DFS read/write with host buffers (bounce/BAR1) | No — cuFile manages GPU↔host transfer | ✅ Implemented |
| Phase 2 | GPU direct via rdma_info + dfs_*_gpu() | Yes — CaRT HMEM RDMA, no server changes | ✅ Implemented |
| Phase 2b | RDMA key import (avoids double NIC registration) | Yes — rkey from nvidia-fs via ma_rkey | ✅ Implemented (OFI; UCX fallback) |

#### Implemented Features (Items 1–9)

1. **File creation (`O_CREAT`)** — `path_split()` splits absolute path into parent
   directory + basename, looks up parent via `dfs_lookup()`, then creates the file
   with `dfs_open(parent, name, S_IFREG|0644, flags)`. Supports nested paths
   (parent directories must exist).

2. **Open flags** — Full support for POSIX-style flags:
   - `O_CREAT` — create file if it doesn't exist
   - `O_EXCL` — fail with `EEXIST` if file already exists (with `O_CREAT`)
   - `O_TRUNC` — truncate existing file to zero length via `dfs_punch()`
   - `O_APPEND` — stored in handle for future offset management
   - `O_RDONLY`, `O_WRONLY`, `O_RDWR` — access mode passed to DFS

3. **Consistent error handling** — Read/write callbacks return `-errno` (negative)
   as cuFile expects. Public APIs (`daos_cufile_open()`, etc.) return positive
   `errno` values consistent with DFS conventions. Zero-size I/O returns 0
   immediately.

4. **Thread safety** — Each `daos_cufile_handle_t` contains a `pthread_mutex_t`
   protecting all DFS operations. Concurrent reads/writes on the same handle are
   safely serialized.

5. **Concurrent I/O** — Mutex per handle ensures correct behavior when multiple
   threads share a file handle.

6. **fd-based mode (Mode 1)** — When used with dfuse, the plugin lazily resolves
   fd → DFS file handle via dfuse handle-export ioctls and `*_global2local()`.
   Applications need only 2 lines different from kernel-GDS pattern. Data
   bypasses FUSE entirely — goes directly via DFS to DAOS servers.

7. **Mount caching with refcounting** — Multiple `daos_cufile_connect()` calls
   to the same pool/container return the same mount with an incremented
   refcount. Only when the last reference is released is the DFS connection
   actually closed. Thread-safe global cache using `d_list_t` with a
   `pthread_mutex_t`.

8. **Lazy init from environment variables** — When pool/container are not
   specified explicitly, the plugin auto-connects using `DAOS_POOL` and
   `DAOS_CONT` environment variables. The auto-connected mount goes through
   the cache (item 7) and is auto-disconnected on deregister.

9. **Graceful reconnect on pool eviction** — When I/O returns errors indicating
   stale handles (`EBADF`, `ENOTCONN`, `EIO`), automatically reconnects the
   DFS mount, reopens the file, and retries the operation once. File path is
   stored in the handle for reconnection. Mount-level lock prevents concurrent
   reconnection races. Only active for Mode 2 handles; Mode 1 (fd-based)
   does not reconnect since dfuse itself does not reconnect.

#### Remaining Work

| # | Item | Category | Priority | Status |
|---|------|----------|----------|--------|
| 10 | Wire SConscript into parent build (conditional on CUDA) | Build | High | ✅ Done |
| 11 | Unit tests (cmocka, no GPU required) | Testing | High | |
| 12 | Integration test with dfuse + cuFile compat mode | Testing | Medium | |
| 13 | Full GPU path tests | Testing | Low | |
| 14 | RPM/DEB packaging for `libdaos_cufile` | Packaging | Low | |
| 15 | Man page / API reference docs | Documentation | Low | |

### Testing the cuFile Plugin

#### Environment Requirements

| Component | Required? | Purpose |
|-----------|-----------|---------|
| DAOS server (single node OK) | ✅ | Storage backend |
| DAOS client libraries | ✅ | `libdaos`, `libdfs`, `libgurt` |
| CUDA Toolkit ≥ 12.2 | For cuFile integration | `libcufile.so` + `cufile.h` headers |
| NVIDIA GPU | ❌ | cuFile uses compat mode without GPU |
| `nvidia-fs.ko` kernel module | ❌ | Not needed; cuFile auto-falls back to compat mode |
| InfiniBand / RoCE | ❌ | cuFile works over TCP; RDMA improves throughput |

CUDA Toolkit is free to download:
```bash
# Rocky Linux 9 / RHEL 9:
dnf config-manager --add-repo \
    https://developer.download.nvidia.com/compute/cuda/repos/rhel9/x86_64/cuda-rhel9.repo
dnf install cuda-toolkit-12-8

# Or just the cuFile component:
dnf install libcufile-devel
```

#### Test Levels

| Level | Needs GPU? | Needs cuFile? | What it validates |
|-------|-----------|--------------|-------------------|
| Unit (direct callback calls) | ❌ | ❌ | DFS read/write via plugin callbacks |
| Integration (cuFile compat mode) | ❌ | ✅ | Full cuFile → plugin → DFS path |
| Full GPU direct | ✅ | ✅ | GPU memory + cuFile + DAOS end-to-end |

#### Level 1: Unit Test (no GPU, no cuFile)

Test the plugin's DFS logic by calling callbacks directly:
```c
#include <daos_cufile.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>

int main() {
    daos_cufile_reg_t *reg;
    CUfileHandle_t     cfh;
    char buf[4096] = {0};

    /* Register with DAOS_POOL/DAOS_CONT env vars */
    daos_cufile_register("/test_file", O_RDWR | O_CREAT, NULL, NULL, &cfh, &reg);

    /* Call callbacks directly — simulates what cuFile does internally */
    CUfileFSOps_t *ops = (CUfileFSOps_t *)daos_cufile_get_ops();
    void *handle = ((CUfileDescr_t *)daos_cufile_get_desc(reg))->handle.handle;

    ops->write(handle, "hello GPU world", 15, 0, NULL);
    ssize_t n = ops->read(handle, buf, sizeof(buf), 0, NULL);
    printf("Read %zd bytes: '%s'\n", n, buf);

    daos_cufile_deregister(reg);
}
```

Build: `gcc test.c -ldaos_cufile -ldaos -ldfs -lgurt -o test`

#### Level 2: Integration (cuFile compat mode, no GPU)

```bash
# Build plugin + examples
scons install

# Mode 1 (requires dfuse mounted at /mnt/daos):
./install/bin/daos_cufile_example /mnt/daos/test_file.dat

# Mode 2 (no dfuse needed):
export DAOS_POOL=mypool DAOS_CONT=mycont
./install/bin/daos_cufile_mode2_example /data/test_file.dat
```

#### Level 3: Full GPU Path

Requires NVIDIA GPU + CUDA driver. Same binary as Level 2 — cuFile
auto-detects GPU capability and uses GPUDirect when available.

---

## cuObject Plugin (Planned)

### Overview

NVIDIA released **cuObject** (v1.0.0, Jan 2026) — a library for direct RDMA
transfers between GPU memory and S3-compatible object storage. It is part of the
GPUDirect Storage family and ships with CUDA Toolkit 13.1+.

cuObject provides a **callback-based architecture** where the storage backend
implements GET/PUT callbacks for the control path, while cuObject handles all
RDMA data-path mechanics (memory registration, GPUDirect RDMA, multipathing).

- Docs: https://docs.nvidia.com/gpudirect-storage/cuobject/index.html
- API: https://docs.nvidia.com/gpudirect-storage/cuobject/cuObjClient-api/index.html

### cuObject Architecture

cuObject uses a callback-based architecture. The storage backend implements
GET/PUT callbacks; cuObject handles GPU memory registration, I/O chunking, and
RDMA multipathing.

#### Core API (from cuObjClient-api docs)

```cpp
/* Callback operations — the storage backend implements these */
typedef struct CUObjIOOps {
    ssize_t (*get)(const void* handle, char* ptr, size_t size, loff_t offset,
                   const cufileRDMAInfo_t* rdma_info);
    ssize_t (*put)(const void* handle, const char* ptr, size_t size, loff_t offset,
                   const cufileRDMAInfo_t* rdma_info);
} CUObjOps_t;

/* Client class — manages memory registration and I/O dispatch */
class cuObjClient {
public:
    cuObjClient(CUObjOps_t& ops, cuObjProto_t proto = CUOBJ_PROTO_RDMA_DC_V1);

    /* Memory management */
    cuObjErr_t cuMemObjGetDescriptor(void* ptr, size_t size);  /* register */
    cuObjErr_t cuMemObjPutDescriptor(void* ptr);               /* deregister */
    ssize_t    cuMemObjGetMaxRequestCallbackSize(void* ptr);

    /* I/O operations (synchronous) */
    ssize_t cuObjGet(void* ctx, void* ptr, size_t size,
                     loff_t offset = 0, loff_t buf_offset = 0);
    ssize_t cuObjPut(void* ctx, void* ptr, size_t size,
                     loff_t offset = 0, loff_t buf_offset = 0);

    /* Utilities */
    static void* getCtx(const void* handle);  /* extract ctx in callback */
    static cuObjMemoryType_t getMemoryType(const void* ptr);
    bool isConnected(void);
};
```

#### Key API Semantics

1. **`offset`/`buf_offset` in `cuObjGet`/`cuObjPut` are reserved** — must be 0.
   Only the callback's `offset` parameter is meaningful (per-chunk start offset).

2. **Automatic chunking** — cuObject splits large I/O into
   `MaxRequestCallbackSize` chunks. Each callback invocation receives one chunk
   with its own `offset` and `size`. Our plugin handles each chunk independently.

3. **Memory registration requirements**:
   - System (host) memory: MUST be registered with `cuMemObjGetDescriptor()`
   - CUDA device memory: can be registered (RDMA) or unregistered (bounce buffer)
   - CUDA managed memory: CANNOT be registered; uses bounce buffer mode
   - Max registration: 4 GiB - 64 KiB per buffer

4. **Thread safety** — callbacks may be invoked from threads other than the
   caller. Our plugin must use proper locking on shared state.

5. **Retryable errors** — returning specific negative errno values
   (`-ETIMEDOUT`, `-ECONNRESET`, `-EIO`, `-EAGAIN`, etc.) triggers cuObject's
   built-in RDMA multipath retry. Our plugin returns `-EIO` on DAOS failures
   to enable retry.

6. **`rdma_info->desc_str`** — an RDMA descriptor string for out-of-band server
   communication. Valid only for the duration of the callback. Our plugin
   ignores this and uses `ptr` directly with Path 1.

### Plugin Design

#### Key Insight: Reuse Path 1 Instead of RDMA Tokens

The cuObject callbacks receive **both** `ptr` (host/GPU buffer) and `rdma_info`
(RDMA token). The `ptr` parameter contains a valid, accessible buffer — cuObject
registers GPU memory and provides a CPU-accessible mapping via GDRCopy or managed
memory.

This means a DAOS cuObject plugin can **ignore `rdma_info` entirely** and use the
`ptr` buffer with existing Path 1 APIs (`daos_obj_fetch_gpu()` /
`daos_obj_update_gpu()`). This eliminates all server-side work:

- ❌ No new RPC handler for RDMA tokens
- ❌ No cuObjServer sidecar
- ❌ No ibverbs integration in DAOS engine
- ✅ Reuses existing CaRT/Mercury HMEM bulk transfer path
- ✅ Works with any libfabric provider (not just InfiniBand)

The trade-off: we lose cuObject's native multipath RDMA and rely on CaRT's
transport instead. For most DAOS deployments this is acceptable since CaRT
already supports RDMA over libfabric/UCX.

#### Plugin Architecture

```
┌─────────────────────────────────────────────────────────────┐
│  AI Application                                              │
│  cuObjPut(ctx, gpu_ptr, 64MB)                               │
└──────────────────────────┬──────────────────────────────────┘
                           │
┌──────────────────────────▼──────────────────────────────────┐
│  cuObjClient (NVIDIA library)                                │
│  - Registers GPU memory, generates RDMA token               │
│  - Invokes PUT callback with ptr + rdma_info                │
│  - Splits large I/O into MaxRequestCallbackSize chunks      │
└──────────────────────────┬──────────────────────────────────┘
                           │  callback
┌──────────────────────────▼──────────────────────────────────┐
│  DAOS cuObject Plugin (libdaos_cuobj.so)                     │
│                                                              │
│  put() callback:                                             │
│    1. Ignore rdma_info (not used)                            │
│    2. Use ptr buffer directly                                │
│    3. Call daos_obj_update_gpu(ptr) via Path 1               │
│       → CaRT bulk with HMEM → Mercury RDMA → DAOS server    │
│                                                              │
│  get() callback:                                             │
│    1. Call daos_obj_fetch_gpu(ptr) via Path 1                │
│    2. Data lands directly in GPU memory via CaRT RDMA        │
└─────────────────────────────────────────────────────────────┘
```

#### Implementation

The plugin implements `CUObjOps_t` callbacks that translate cuObject
operations into DAOS object I/O using Path 1:

```cpp
#include <daos.h>
#include <daos_obj.h>
/* cuObjClient header (CUDA 13.1+) */
#include <cuObjClient.h>

/**
 * DAOS context — passed as ctx to cuObjGet/cuObjPut, retrieved via getCtx()
 * in callbacks.
 *
 * Note: callbacks may be invoked from different threads (per cuObject docs).
 * The context must either be thread-safe or unique per operation.
 */
typedef struct {
    daos_handle_t  oh;      /* object handle */
    daos_key_t     dkey;    /* distribution key */
    daos_key_t     akey;    /* attribute key */
    daos_handle_t  th;      /* transaction handle (DAOS_TX_NONE for no-tx) */
} daos_cuobj_ctx_t;

/**
 * PUT callback — called by cuObjClient for each chunk of a cuObjPut().
 *
 * Parameters (from cuObject API docs):
 *   handle    — opaque cookie; use cuObjClient::getCtx(handle) to get ctx
 *   ptr       — accessible buffer containing data for this chunk
 *   size      — size of this chunk (≤ MaxRequestCallbackSize)
 *   offset    — starting object offset for this chunk (0-based)
 *   rdma_info — RDMA descriptor (rdma_info->desc_str); ignored by our plugin
 *
 * Returns: bytes written on success, negative errno on failure.
 *   Returning -EIO/-ETIMEDOUT/-ECONNRESET triggers cuObject multipath retry.
 */
ssize_t daos_cuobj_put(const void* handle, const char* ptr, size_t size,
                       loff_t offset, const cufileRDMAInfo_t* rdma_info) {
    daos_cuobj_ctx_t* ctx = (daos_cuobj_ctx_t*)cuObjClient::getCtx(handle);

    if (ctx == nullptr)
        return -EINVAL;

    (void)rdma_info;  /* ignored — Path 1 handles RDMA via CaRT/Mercury */

    d_iov_t iov;
    d_sg_list_t sgl;
    daos_iod_t iod = {};
    daos_mem_attr_t mem_attr = {
        .ma_mem_type  = DAOS_MEM_TYPE_CUDA,
        .ma_device_id = 0,
    };

    d_iov_set(&iov, (void *)ptr, size);
    sgl.sg_nr   = 1;
    sgl.sg_nr_out = 0;
    sgl.sg_iovs = &iov;

    /* Map callback offset directly to DAOS extent */
    daos_recx_t recx = { .rx_idx = (uint64_t)offset, .rx_nr = size };
    iod.iod_recxs = &recx;
    iod.iod_nr    = 1;
    iod.iod_size  = 1;
    iod.iod_type  = DAOS_IOD_ARRAY;
    d_iov_set(&iod.iod_name, ctx->akey.iov_buf, ctx->akey.iov_len);

    int rc = daos_obj_update_gpu(ctx->oh, ctx->th, 0, &ctx->dkey,
                                 1, &iod, &sgl, &mem_attr, NULL);
    if (rc != 0)
        return -EIO;  /* retryable — triggers cuObject multipath */

    return (ssize_t)size;
}

/**
 * GET callback — symmetric to put, fetches into GPU buffer via Path 1.
 */
ssize_t daos_cuobj_get(const void* handle, char* ptr, size_t size,
                       loff_t offset, const cufileRDMAInfo_t* rdma_info) {
    daos_cuobj_ctx_t* ctx = (daos_cuobj_ctx_t*)cuObjClient::getCtx(handle);

    if (ctx == nullptr)
        return -EINVAL;

    (void)rdma_info;

    d_iov_t iov;
    d_sg_list_t sgl;
    daos_iod_t iod = {};
    daos_mem_attr_t mem_attr = {
        .ma_mem_type  = DAOS_MEM_TYPE_CUDA,
        .ma_device_id = 0,
    };

    d_iov_set(&iov, ptr, size);
    sgl.sg_nr   = 1;
    sgl.sg_nr_out = 0;
    sgl.sg_iovs = &iov;

    daos_recx_t recx = { .rx_idx = (uint64_t)offset, .rx_nr = size };
    iod.iod_recxs = &recx;
    iod.iod_nr    = 1;
    iod.iod_size  = 1;
    iod.iod_type  = DAOS_IOD_ARRAY;
    d_iov_set(&iod.iod_name, ctx->akey.iov_buf, ctx->akey.iov_len);

    int rc = daos_obj_fetch_gpu(ctx->oh, ctx->th, 0, &ctx->dkey,
                                1, &iod, &sgl, &mem_attr, NULL, NULL);
    if (rc != 0)
        return -EIO;

    return (ssize_t)size;
}

/* Application usage */
void example_gpu_to_daos(daos_handle_t oh, daos_key_t dkey, daos_key_t akey) {
    CUObjOps_t ops = { .get = daos_cuobj_get, .put = daos_cuobj_put };
    cuObjClient client(ops, CUOBJ_PROTO_RDMA_DC_V1);

    if (!client.isConnected()) {
        fprintf(stderr, "cuObjClient connection failed\n");
        return;
    }

    /* Allocate and register GPU memory (required for RDMA) */
    void* gpu_buf;
    cudaMalloc(&gpu_buf, 64 * 1024 * 1024);
    client.cuMemObjGetDescriptor(gpu_buf, 64 * 1024 * 1024);

    /*
     * Note: cuObjPut offset/buf_offset are RESERVED (must be 0).
     * cuObject handles chunking internally — if 64MB > MaxRequestCallbackSize,
     * the PUT callback is invoked multiple times with per-chunk offsets.
     */
    daos_cuobj_ctx_t ctx = { .oh = oh, .dkey = dkey, .akey = akey,
                             .th = DAOS_TX_NONE };
    ssize_t ret = client.cuObjPut(&ctx, gpu_buf, 64 * 1024 * 1024, 0, 0);
    if (ret < 0)
        fprintf(stderr, "cuObjPut failed: %zd\n", ret);

    client.cuMemObjPutDescriptor(gpu_buf);
    cudaFree(gpu_buf);
}
```

### Server-Side Requirements

**None.** The "reuse Path 1" approach eliminates all server-side work. If native
RDMA token performance is ever needed, a server RPC handler could be added as an
optimization (see Future Work).

### Comparison: Native API vs cuObject

| Aspect | Native (`daos_obj_*_gpu`) | cuObject Plugin (Path 1 reuse) |
|--------|---------------------------|--------------------------------|
| RDMA management | CaRT/Mercury (FI_HMEM) | CaRT/Mercury (via Path 1) |
| Transport | libfabric or UCX (any provider) | Same (any libfabric provider) |
| API style | C, DAOS-native | C++ wrapper over C DAOS APIs |
| Server changes | Minimal (observability only) | **None** (reuses Path 1) |
| GPU mem registration | Mercury + libfabric | cuObjClient + Mercury (double reg) |
| TCP fallback | Yes (with bounce buffer) | Yes (same as Path 1) |
| Max transfer size | Unlimited (SGL-based) | 4 GiB - 64 KiB per cuObj chunk |
| Multipathing | Not built-in | cuObjClient built-in (unused) |
| Best for | Existing DAOS API users | S3-style AI/ML data pipelines |

**Note:** The cuObject plugin ignores `rdma_info` tokens and uses `ptr` buffers
with `daos_obj_*_gpu()`. GPU memory may be registered twice (once by cuObjClient,
once by CaRT/Mercury), but this has negligible overhead since registration is
cached.

### Implementation Plan

The cuObject plugin is client-side only — no server modifications required.

| # | Work Item | Effort | Priority |
|---|-----------|--------|----------|
| 1 | Define `daos_cuobj_ctx_t` and thin C wrapper for C++ callbacks | Small | High |
| 2 | Implement `daos_cuobj_get()` callback using `daos_obj_fetch_gpu()` | Small | High |
| 3 | Implement `daos_cuobj_put()` callback using `daos_obj_update_gpu()` | Small | High |
| 4 | Object key mapping: cuObject URI → DAOS (oid, dkey, akey) | Medium | High |
| 5 | Connection management: pool/container handle lifecycle (reuse cuFile cache) | Medium | High |
| 6 | Error translation: `-DER_*` → retryable errno (`-EIO`, `-ETIMEDOUT`) for multipath | Small | High |
| 7 | Thread safety: callbacks from different threads (per cuObject docs §1.18) | Small | High |
| 8 | Build system: `src/client/cuobj/SConscript` (conditional on CUDA 13.1+) | Small | Medium |
| 9 | `cuObjClient` initialization, `isConnected()` check, JSON config | Small | Medium |
| 10 | Handle `MaxRequestCallbackSize` chunking (verify callback offset handling) | Small | Medium |
| 11 | Unit tests (mock cuObjClient, test callback logic with host memory) | Medium | Medium |
| 12 | Integration tests with real cuObjClient library | Medium | Low |
| 13 | Performance benchmarks: compare cuObj+DAOS vs native DAOS | Medium | Low |

**Total estimated effort:** ~2 weeks (items 1–7 for MVP, 8–13 for production)

Items 1–3 are straightforward since the plugin is essentially a translation layer
between cuObject's C++ callback API and DAOS's C object API via the existing
Path 1 `daos_obj_*_gpu()` functions. cuObject handles I/O chunking automatically
(splits into `MaxRequestCallbackSize` chunks), so our callbacks only process one
chunk at a time — no multi-chunk loop needed (unlike the cuFile GPU direct path).

---


## Code Reuse: cuFile ↔ cuObject

Both plugins share infrastructure that should be factored into a common library
(`src/client/gpu_common/`) when cuObject implementation begins:

| Component | Currently in cuFile | Reusable? |
|---|---|---|
| Mount cache (refcounted pool/container) | `mount_cache_*` functions | ✅ Same pattern |
| Lazy init from `DAOS_POOL`/`DAOS_CONT` | `daos_cufile_register(path, flags, NULL, NULL, ...)` | ✅ Identical |
| Reconnection detection | `is_reconnectable_error()` | ✅ Same errno set |
| `cufileRDMAInfo_t` type definition | local typedef | ✅ Same struct |

Key differences that prevent naive sharing:
- cuFile uses **DFS** (`dfs_connect`, `dfs_read_gpu`, `dfs_obj_t`) — file-level
- cuObject uses **daos_obj** (`daos_pool_connect`, `daos_obj_fetch_gpu`) — object-level
- cuFile handles **chunk-boundary splitting** via `dfs_read_gpu`/`dfs_write_gpu`;
  cuObject does not (cuObjClient handles chunking, callback receives single chunk)
- cuFile reconnection reopens DFS files by path; cuObject would reopen objects by OID

Planned directory layout when cuObject is implemented:
```
src/client/
├── gpu_common/          ← shared: mount cache, env helpers, reconnect base
│   ├── daos_gpu_common.h
│   ├── daos_gpu_common.c
│   └── SConscript
├── cufile/              ← DFS-backed cuFile plugin (existing)
└── cuobj/               ← object-backed cuObject plugin (future)
```

---

## Runtime Configuration

| Environment Variable | Default | Description |
|---------------------|---------|-------------|
| `DAOS_POOL` | (none) | Pool label for cuFile lazy init (auto-connect when mount=NULL) |
| `DAOS_CONT` | (none) | Container label for cuFile lazy init |

## Dependencies

| Component | Version | Plugin | Purpose |
|-----------|---------|--------|---------|
| CUDA Toolkit | ≥ 12.2 | cuFile | `cufile.h` headers |
| libcufile | ≥ 1.8 (CUDA 12.2+) | cuFile | cuFile runtime (for integration testing) |
| cuObjClient | ≥ 1.0.0 (CUDA 13.1+) | cuObject | cuObject client library |
| nvidia-peermem | (kernel module) | Both | GPU RDMA (only for true GPU direct, not compat mode) |

**Minimum for development/testing:** DAOS cluster + dfuse mount + CUDA Toolkit
(for `cufile.h` headers). No GPU hardware needed — cuFile automatically uses
compatibility mode (bounce buffers) when no GPU is present.

## References

- [NVIDIA GDS Architecture](https://docs.nvidia.com/gpudirect-storage/overview-guide/)
- [nvidia-fs kernel module source](https://github.com/NVIDIA/gds-nvidia-fs)
- [cuFile API Reference Guide](https://docs.nvidia.com/gpudirect-storage/api-reference-guide/index.html)
- [NVIDIA cuObject Documentation](https://docs.nvidia.com/gpudirect-storage/cuobject/index.html)
- [cuObjClient API](https://docs.nvidia.com/gpudirect-storage/cuobject/cuObjClient-api/index.html)
