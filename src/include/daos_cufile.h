/**
 * (C) Copyright 2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

/**
 * \file
 * DAOS cuFile Userspace Filesystem Plugin
 *
 * This plugin implements NVIDIA's CUfileFSOps_t interface, allowing applications
 * using the cuFile API (GDS) to access DAOS storage transparently.
 *
 * Recommended usage (transparent — application only uses cuFile APIs):
 *   1. Set DAOS_POOL and DAOS_CONT environment variables
 *   2. Call daos_cufile_register(path, flags, &cfh) — one call does everything
 *   3. Use standard cuFileRead()/cuFileWrite() — no DAOS knowledge needed
 *   4. Call daos_cufile_deregister(cfh) to clean up
 *
 * Advanced usage (explicit control over connection):
 *   1. Call daos_cufile_connect() to establish DAOS pool/container connection
 *   2. Call daos_cufile_open() to open a file path
 *   3. Call daos_cufile_get_ops() and register with cuFileHandleRegister()
 *   4. Use standard cuFileRead()/cuFileWrite() for GPU direct I/O
 *   5. Cleanup with daos_cufile_close() and daos_cufile_disconnect()
 */

#ifndef DAOS_CUFILE_H
#define DAOS_CUFILE_H

#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Opaque handle for a DAOS cuFile mount (pool + container) */
typedef struct daos_cufile_mount  daos_cufile_mount_t;

/** Opaque handle for a DAOS cuFile file */
typedef struct daos_cufile_handle daos_cufile_handle_t;

/** Opaque handle wrapping cuFile registration (for daos_cufile_register) */
typedef struct daos_cufile_reg    daos_cufile_reg_t;

/**
 * Global ops table for fd-based registration (Mode 1).
 *
 * Applications using dfuse can register with cuFile using just:
 *   int fd = open("/mnt/daos/file", O_RDWR);
 *   CUfileDescr_t desc = {0};
 *   desc.type       = CU_FILE_HANDLE_TYPE_USERSPACE_FS;  // = 3
 *   desc.handle.fd  = fd;
 *   desc.fs_ops     = &daos_cufile_ops;
 *   cuFileHandleRegister(&cfh, &desc);
 *   cuFileRead(cfh, gpu_buf, size, 0, 0);
 *
 * The plugin lazily resolves the fd to a DFS file handle on first I/O.
 * Data bypasses FUSE entirely — goes directly via DFS to DAOS servers.
 *
 * Requires: dfuse mounted, link with -ldaos_cufile.
 */
extern const void *daos_cufile_ops;

/*
 * ============================================================================
 * Transparent API — Application only interacts with cuFile after setup
 * ============================================================================
 *
 * Provides a single-call setup that hides all DAOS internals. After
 * daos_cufile_register(), the application uses only standard cuFile APIs
 * (cuFileRead, cuFileWrite, cuFileReadAsync, cuFileWriteAsync).
 *
 * Connection is established automatically from DAOS_POOL/DAOS_CONT env vars,
 * or from explicit pool/container arguments.
 */

/**
 * Register a DAOS file with cuFile in one call.
 *
 * Performs all setup internally: DAOS init, pool/container connect (from
 * DAOS_POOL/DAOS_CONT env vars or explicit arguments), file open, and
 * cuFileHandleRegister(). Returns a CUfileHandle_t ready for cuFileRead/Write.
 *
 * Example:
 *   CUfileHandle_t cfh;
 *   daos_cufile_reg_t *reg;
 *   daos_cufile_register("/model.pt", O_RDONLY, NULL, NULL, &cfh, &reg);
 *   cuFileRead(cfh, gpu_buf, size, 0, 0);   // standard cuFile!
 *   daos_cufile_deregister(reg);
 *
 * \param[in]  path   File path within the DAOS container.
 * \param[in]  flags  Open flags (O_RDONLY, O_RDWR, O_CREAT, etc.).
 * \param[in]  pool   Pool label, or NULL to use DAOS_POOL env var.
 * \param[in]  cont   Container label, or NULL to use DAOS_CONT env var.
 * \param[out] cfh    Returned CUfileHandle_t for cuFileRead/cuFileWrite.
 * \param[out] reg    Returned registration handle for daos_cufile_deregister().
 *
 * \return 0 on success, errno on failure.
 */
int
daos_cufile_register(const char *path, int flags, const char *pool, const char *cont, void *cfh,
		     daos_cufile_reg_t **reg);

/**
 * Deregister a DAOS cuFile handle and release all resources.
 *
 * Calls cuFileHandleDeregister(), closes the file, and disconnects from DAOS.
 *
 * \param[in] reg  Registration handle from daos_cufile_register().
 */
void
daos_cufile_deregister(daos_cufile_reg_t *reg);

/**
 * Get the CUfileDescr_t from a registration handle.
 *
 * Returns a pointer suitable for passing to cuFileHandleRegister(). This
 * allows full transparency without the DAOS app needing to construct the
 * descriptor manually.
 *
 * Example (complete transparent usage):
 *   daos_cufile_reg_t *reg;
 *   CUfileHandle_t cfh;
 *
 *   daos_cufile_register("/model.pt", O_RDONLY, NULL, NULL, &cfh, &reg);
 *   cuFileHandleRegister(&cfh, daos_cufile_get_desc(reg));
 *   cuFileRead(cfh, gpu_buf, size, 0, 0);
 *   cuFileHandleDeregister(cfh);
 *   daos_cufile_deregister(reg);
 *
 * \param[in] reg  Registration handle from daos_cufile_register().
 *
 * \return Pointer to CUfileDescr_t, or NULL if reg is NULL.
 */
const void *
daos_cufile_get_desc(daos_cufile_reg_t *reg);

/**
 * Get the CUfileFSOps_t pointer for registering with cuFile.
 *
 * The returned pointer is a static singleton — valid for the process lifetime.
 * Use it when constructing a CUfileDescr_t manually (Mode 2):
 *
 *   CUfileDescr_t desc = {
 *       .type = CU_FILE_HANDLE_TYPE_USERSPACE_FS,
 *       .handle.handle = (void *)handle,
 *       .fs_ops = daos_cufile_get_ops(),
 *   };
 *
 * \return Pointer to the DAOS CUfileFSOps_t operations table.
 */
const void *
daos_cufile_get_ops(void);

#ifdef __cplusplus
}
#endif

#endif /* DAOS_CUFILE_H */
