/**
 * (C) Copyright 2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

/**
 * \file
 * Internal shared definitions for the DAOS cuFile plugin.
 * Not part of the public API.
 */

#ifndef CUFILE_INTERNAL_H
#define CUFILE_INTERNAL_H

#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <daos.h>
#include <daos_fs.h>
#include <daos_obj.h>
#include <gurt/common.h>

#include <daos_cufile.h>

/*
 * Use NVIDIA's cufile.h if available, otherwise local ABI-compatible defs.
 */
#if __has_include(<cufile.h>)
#include <cufile.h>
#else
typedef struct cufileRDMAInfo {
	int         version;
	int         desc_len;
	const char *desc_str;
} cufileRDMAInfo_t;

typedef struct CUfileFSOps {
	const char *(*fs_type)(void *handle);
	int (*getRDMADeviceList)(void *handle, void **hostaddrs);
	int (*getRDMADevicePriority)(void *handle, char *, size_t, loff_t, void *hostaddr);
	ssize_t (*read)(void *handle, char *, size_t, loff_t, cufileRDMAInfo_t *);
	ssize_t (*write)(void *handle, const char *, size_t, loff_t, cufileRDMAInfo_t *);
} CUfileFSOps_t;

enum CUfileFileHandleType {
	CU_FILE_HANDLE_TYPE_OPAQUE_FD    = 1,
	CU_FILE_HANDLE_TYPE_OPAQUE_WIN32 = 2,
	CU_FILE_HANDLE_TYPE_USERSPACE_FS = 3,
};

typedef struct CUfileDescr {
	enum CUfileFileHandleType type;
	union {
		int   fd;
		void *handle;
	} handle;
	const CUfileFSOps_t *fs_ops;
} CUfileDescr_t;
#endif /* __has_include(<cufile.h>) */

/** Internal mount structure */
struct daos_cufile_mount {
	dfs_t          *cfm_dfs;
	daos_handle_t   cfm_poh;
	daos_handle_t   cfm_coh;
	bool            cfm_connected;
	char           *cfm_pool_label;
	char           *cfm_cont_label;
	pthread_mutex_t cfm_lock;       /* protects reconnection */
};

/** Internal file handle */
struct daos_cufile_handle {
	daos_cufile_mount_t *cfh_mount;
	dfs_obj_t           *cfh_obj;
	int                  cfh_flags;
	char                *cfh_path;       /* stored for reconnection */
	bool                 cfh_auto_mount; /* true if lazily connected */
	bool                 cfh_fd_mode;    /* true if resolved from fd (Mode 1) */
};

/** Registration state for the transparent API */
struct daos_cufile_reg {
	daos_cufile_handle_t *dfh;
	CUfileDescr_t         desc;
};

/** Mount cache entry */
struct mount_cache_entry {
	d_list_t             mce_link;
	char                *mce_pool;
	char                *mce_cont;
	daos_cufile_mount_t *mce_mount;
	int                  mce_refcount;
};

/* Mount cache globals (defined in cufile_plugin.c) */
extern d_list_t        mount_cache_list;
extern pthread_mutex_t mount_cache_lock;

/* Mount cache helpers (defined in cufile_plugin.c) */
struct mount_cache_entry *mount_cache_find_locked(const char *pool, const char *cont);
struct mount_cache_entry *mount_cache_insert_locked(const char *pool, const char *cont,
						    daos_cufile_mount_t *mount);
void mount_cache_remove_locked(struct mount_cache_entry *entry);

/* Internal helpers (defined in cufile_plugin.c) */
int path_split(const char *path, char **parent_path, char **base_name);

/* Internal API (defined in cufile_daos.c, not exposed in public header) */
int daos_cufile_connect(const char *pool, const char *cont, daos_cufile_mount_t **mount);
void daos_cufile_disconnect(daos_cufile_mount_t *mount);
int daos_cufile_open(daos_cufile_mount_t *mount, const char *path, int flags,
		     daos_cufile_handle_t **handle);
void daos_cufile_close(daos_cufile_handle_t *handle);

#endif /* CUFILE_INTERNAL_H */
