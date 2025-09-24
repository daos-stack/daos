/**
 * (C) Copyright 2025 Vdura Inc.
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DDB_MGMT_H__
#define __DDB_MGMT_H__

int
ddb_auto_calculate_meta_mount_size(unsigned int *meta_mount_size);

int
ddb_recreate_pooltgts(const char *meta_mount);

int
ddb_clear_dir(const char *dir);

int
ddb_is_mountpoint(const char *path);

int
ddb_dirs_prepare(const char *meta_mount);

int
ddb_mount(const char *meta_mount, unsigned int meta_mount_size);

#endif /** __DDB_MGMT_H__ */
