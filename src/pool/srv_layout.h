/*
 * (C) Copyright 2016-2023 Intel Corporation.
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
 * The version of the whole layout is stored in ds_pool_prop_global_version.
 */

#ifndef __POOL_SRV_LAYOUT_H__
#define __POOL_SRV_LAYOUT_H__

#include <daos_types.h>

/*
 * Root KVS (RDB_KVS_GENERIC): pool properties
 *
 * The ds_pool_prop_global_version property stores the version of the whole
 * layout, including that of the container metadata..
 *
 * The ds_pool_prop_map_buffer property stores the pool map in pool_buf format,
 * because version is absent from pool_buf, it has to be stored separately in
 * ds_pool_prop_map_version.
 *
 * IMPORTANT! Please add new keys to this KVS like this:
 *
 *   extern d_iov_t ds_pool_prop_new_key;	comment_on_value_type
 *
 *   Note 1. The "new_key" name in ds_pool_prop_new_key must not appear in the
 *   root KVS in src/container/srv_layout.h, that is, there must not be a
 *   ds_cont_prop_new_key, because the two root KVSs are the same RDB KVS.
 *
 *   Note 2. The comment_on_value_type shall focus on the value type only;
 *   usage shall be described above in this comment following existing
 *   examples. If the value is another KVS, its type shall be the KVS name.
 */
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
extern d_iov_t ds_pool_prop_ec_cell_sz;		/* uint64_t */
extern d_iov_t ds_pool_prop_redun_fac;		/* uint64_t */
extern d_iov_t ds_pool_prop_ec_pda;		/* uint32_t */
extern d_iov_t ds_pool_prop_rp_pda;		/* uint32_t */
extern d_iov_t ds_pool_attr_user;		/* pool user attribute KVS */
extern d_iov_t ds_pool_prop_policy;		/* string (tiering policy) */
extern d_iov_t ds_pool_prop_global_version;	/* uint32_t */
extern d_iov_t ds_pool_prop_upgrade_status;	/* uint32_t */
extern d_iov_t ds_pool_prop_upgrade_global_version;/* uint32_t */
extern d_iov_t ds_pool_prop_perf_domain;	/* uint32_t */
extern d_iov_t ds_pool_prop_scrub_mode;		/* uint64_t */
extern d_iov_t ds_pool_prop_scrub_freq;		/* uint64_t */
extern d_iov_t ds_pool_prop_scrub_thresh;	/* uint64_t */
extern d_iov_t ds_pool_prop_svc_redun_fac;	/* uint64_t */
extern d_iov_t ds_pool_prop_obj_version;	/* uint32_t */
extern d_iov_t ds_pool_prop_checkpoint_mode;    /* uint32_t */
extern d_iov_t ds_pool_prop_checkpoint_freq;    /* uint32_t */
extern d_iov_t ds_pool_prop_checkpoint_thresh;  /* uint32_t */
extern d_iov_t ds_pool_prop_reint_mode;		/* uint32_t */
/* Please read the IMPORTANT notes above before adding new keys. */

/*
 * Pool handle KVS (RDB_KVS_GENERIC)
 *
 * Each key is a pool handle UUID in uuid_t. Each value is a pool_hdl object
 * defined below.
 */
struct pool_hdl {
	uint64_t	ph_flags;
	uint64_t	ph_sec_capas;
	char		ph_machine[MAXHOSTNAMELEN+1];
	size_t		ph_cred_len;
	char		ph_cred[];
};

/* old format (<= version 2.0) */
struct pool_hdl_v0 {
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
