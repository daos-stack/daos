/*
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * \file
 *
 * ds_pool: Pool Server Storage Layout
 *
 * This header assembles (hopefully) everything related to the persistent
 * storage layout of pool metadata used by ds_pool.
 *
 * In the rdb, we have this layout:
 *
 *     Root KVS (GENERIC):
 *       Pool handle KVS (GENERIC)
 *       Pool user attribute KVS (GENERIC)
 *
 * The version of the whole layout is stored in ds_pool_prop_version.
 */

#ifndef __POOL_SRV_LAYOUT_H__
#define __POOL_SRV_LAYOUT_H__

#include <daos_types.h>

/* Default layout version */
#define DS_POOL_MD_VERSION 4

/* Lowest compatible layout version */
#define DS_POOL_MD_VERSION_LOW 4

/*
 * Root KVS (RDB_KVS_GENERIC): pool properties
 *
 * ds_pool_prop_version stores the version of the whole layout.
 *
 * The pool map is stored in pool_buf format. Because version and target UUID
 * are absent from pool_buf, they have to be stored in ds_pool_prop_map_version
 * and ds_pool_prop_map_uuids, respectively. The target UUIDs are stored in
 * target ID order.
 */
extern d_iov_t ds_pool_prop_version;		/* uint32_t */
extern d_iov_t ds_pool_prop_map_version;	/* uint32_t */
extern d_iov_t ds_pool_prop_map_buffer;		/* pool_buf */
extern d_iov_t ds_pool_prop_label;		/* string */
extern d_iov_t ds_pool_prop_acl;		/* daos_acl */
extern d_iov_t ds_pool_prop_space_rb;		/* uint64_t */
extern d_iov_t ds_pool_prop_self_heal;		/* uint64_t */
extern d_iov_t ds_pool_prop_reclaim;		/* uint64_t */
extern d_iov_t ds_pool_prop_owner;		/* string */
extern d_iov_t ds_pool_prop_owner_group;	/* string */
extern d_iov_t ds_pool_prop_connectable;	/* uint32_t */
extern d_iov_t ds_pool_prop_nhandles;		/* uint32_t */
extern d_iov_t ds_pool_prop_handles;		/* pool handle KVS */
extern d_iov_t ds_pool_prop_ec_cell_sz;		/* pool EC cell size */
extern d_iov_t ds_pool_attr_user;		/* pool user attributes KVS */

/*
 * Pool handle KVS (RDB_KVS_GENERIC)
 *
 * Each key is a pool handle UUID in uuid_t. Each value is a pool_hdl object
 * defined below.
 */
struct pool_hdl {
	uint64_t	ph_flags;
	uint64_t	ph_sec_capas;
};

/*
 * Pool user attribute KVS (RDB_KVS_GENERIC)
 *
 * Each key is a (null-terminated) string. Each value is a user-defined byte
 * array. Sizes of keys (or values) may vary.
 */

extern daos_prop_t pool_prop_default;

/**
 * Initializes the default pool properties.
 *
 * \return	0		Success
 *		-DER_NOMEM	Could not allocate
 */
int ds_pool_prop_default_init(void);

/**
 * Finalizes the default pool properties.
 * Frees any properties that were dynamically allocated.
 */
void ds_pool_prop_default_fini(void);

#endif /* __POOL_SRV_LAYOUT_H__ */
