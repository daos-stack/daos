/**
 * (C) Copyright 2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * libpil4dfs internal head file
 */

#ifndef PIL4DFS_INT_H
#define PIL4DFS_INT_H

#include "dfs_dcache.h"

#define MAX_MMAP_BLOCK      (64)
#define MAX_OPENED_FILE     (2048)
#define MAX_OPENED_FILE_M1  ((MAX_OPENED_FILE)-1)
#define MAX_OPENED_DIR      (512)
#define MAX_OPENED_DIR_M1   ((MAX_OPENED_DIR)-1)

#define MAX_EQ          64

/* Use very large synthetic FD to distinguish regular FD from Kernel */

/* FD_FILE_BASE - The base number of the file descriptor for a regular file.
 * The fd allocate from this lib is always larger than FD_FILE_BASE.
 */
#define FD_FILE_BASE    (0x20000000)

/* FD_FILE_BASE - The base number of the file descriptor for a directory.
 * The fd allocate from this lib is always larger than FD_FILE_BASE.
 */
#define FD_DIR_BASE     (0x40000000)

/* structure allocated for a FD for a file */
struct file_obj {
	struct dfs_mt     *dfs_mt;
	dfs_obj_t         *file;
	struct dcache_rec *parent;
	int                open_flag;
	int                ref_count;
	unsigned int       st_ino;
	int                idx_mmap;
	off_t              offset;
	char              *path;
	char               item_name[DFS_MAX_NAME];
};

/* structure allocated for a FD for a dir */
struct dir_obj {
	int              fd;
	uint32_t         num_ents;
	dfs_obj_t       *dir;
	long int         offset;
	struct dfs_mt   *dfs_mt;
	int              open_flag;
	int              ref_count;
	unsigned int     st_ino;
	daos_anchor_t    anchor;
	/* path and ents will be allocated together dynamically since they are large. */
	char            *path;
	struct dirent   *ents;
};

struct mmap_obj {
	/* The base address of this memory block */
	char            *addr;
	size_t           length;
	/* the size of file. It is needed when write back to storage. */
	size_t           file_size;
	int              prot;
	int              flags;
	/* The fd used when mmap is called */
	int              fd;
	/* num_pages = length / page_size */
	int              num_pages;
	int              num_dirty_pages;
	off_t            offset;
	/* An array to indicate whether a page is updated or not */
	bool            *updated;
};

/* structure allocated for dfs container */
struct dfs_mt {
	dfs_t           *dfs;
	daos_handle_t    poh;
	daos_handle_t    coh;
	dfs_dcache_t    *dcache;
	int              len_fs_root;
	_Atomic uint32_t inited;
	char            *pool, *cont;
	char            *fs_root;
};

#endif
