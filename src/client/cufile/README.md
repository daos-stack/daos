# DAOS cuFile Plugin (libdaos_cufile)

A userspace filesystem plugin for NVIDIA cuFile (GPUDirect Storage) that enables
GPU direct I/O with DAOS as the storage backend.

## Overview

This plugin implements NVIDIA's `CUfileFSOps_t` interface using
`CU_FILE_HANDLE_TYPE_USERSPACE_FS`, allowing any application that uses cuFile
APIs to transparently access DAOS without modification.

## How It Works

```
Application → cuFileRead()/cuFileWrite() → cuFile library
    → CUfileFSOps_t callbacks → libdaos_cufile → DFS → DAOS
```

**Phase 1 (current):** cuFile manages GPU↔host bounce buffers. The plugin's
`read()`/`write()` callbacks receive host memory pointers and call
`dfs_read()`/`dfs_write()`. No server changes needed.

**Phase 2 (future):** When `cufileRDMAInfo_t` is provided, use RDMA tokens for
true zero-copy transfer between GPU and DAOS server.

## Building

```bash
# Build with DAOS (plugin is built as part of the client)
scons install

# The library installs as:
#   $PREFIX/lib64/libdaos_cufile.so
#   $PREFIX/include/cufile.h
```

## Usage

```c
#include <cufile.h>
#include <cufile.h>

/* Connect to DAOS */
daos_cufile_mount_t *mount;
daos_cufile_connect("mypool", "mycont", &mount);

/* Open a file */
daos_cufile_handle_t *dfh;
daos_cufile_open(mount, "/data/model.pt", O_RDONLY, &dfh);

/* Register with cuFile */
CUfileDescr_t desc = {
    .type = CU_FILE_HANDLE_TYPE_USERSPACE_FS,
    .handle.handle = (void *)dfh,
    .fs_ops = daos_cufile_get_ops(),
};
CUfileHandle_t cfh;
cuFileHandleRegister(&cfh, &desc);

/* GPU direct read via standard cuFile API */
void *gpu_buf;
cudaMalloc(&gpu_buf, size);
cuFileBufRegister(gpu_buf, size, 0);
cuFileRead(cfh, gpu_buf, size, 0, 0);  /* Data goes directly to GPU */

/* Cleanup */
cuFileBufDeregister(gpu_buf);
cudaFree(gpu_buf);
cuFileHandleDeregister(cfh);
daos_cufile_close(dfh);
daos_cufile_disconnect(mount);
```

## Compatible Frameworks

Any framework using cuFile APIs benefits automatically:
- **NVIDIA DALI** — GPU data loading for training
- **KvikIO** (RAPIDS) — Python bindings for cuFile
- **cuDF** — GPU DataFrame library
- **PyTorch** (with GDS DataLoader)
- **Custom applications** using `cuFileRead`/`cuFileWrite`

## Dependencies

- DAOS client libraries (`libdaos`, `libdfs`, `libgurt`)
- CUDA Toolkit ≥ 12.2 (for cuFile with userspace FS support)
- No GPU hardware required for the plugin build itself

## API Reference

| Function | Description |
|----------|-------------|
| `daos_cufile_connect(pool, cont, &mount)` | Connect to DAOS pool/container |
| `daos_cufile_disconnect(mount)` | Disconnect and release resources |
| `daos_cufile_open(mount, path, flags, &handle)` | Open a file by path |
| `daos_cufile_close(handle)` | Close file handle |
| `daos_cufile_get_ops()` | Get `CUfileFSOps_t` pointer for registration |
