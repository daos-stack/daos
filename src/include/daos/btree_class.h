/**
 * (C) Copyright 2016-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * dbtree Classes
 */

#ifndef __DAOS_SRV_BTREE_CLASS_H__
#define __DAOS_SRV_BTREE_CLASS_H__

#include <daos/common.h>
#include <daos/btree.h>
#include <daos_types.h>

/* name-value: hash-ordered keys */
#define DBTREE_CLASS_NV (DBTREE_DSM_BEGIN + 0)
extern btr_ops_t dbtree_nv_ops;
int
dbtree_nv_update(daos_handle_t tree, const void *key, size_t key_size, const void *value,
		 size_t size);
int
dbtree_nv_lookup(daos_handle_t tree, const void *key, size_t key_size, void *value, size_t size);
int
dbtree_nv_lookup_ptr(daos_handle_t tree, const void *key, size_t key_size, void **value,
		     size_t *size);
int
dbtree_nv_delete(daos_handle_t tree, const void *key, size_t key_size);
int
dbtree_nv_create_tree(daos_handle_t tree, const void *key, size_t key_size, unsigned int class,
		      uint64_t feats, unsigned int order, daos_handle_t *tree_new);
int
dbtree_nv_open_tree(daos_handle_t tree, const void *key, size_t key_size,
		    daos_handle_t *tree_child);
int
dbtree_nv_destroy_tree(daos_handle_t tree, const void *key, size_t key_size);
int
dbtree_nv_destroy(daos_handle_t tree, const void *key, size_t key_size);

/* uuid_t-value: unordered keys */
#define DBTREE_CLASS_UV (DBTREE_DSM_BEGIN + 1)
extern btr_ops_t dbtree_uv_ops;
int
dbtree_uv_update(daos_handle_t tree, const uuid_t uuid, const void *value, size_t size);
int
dbtree_uv_lookup(daos_handle_t tree, const uuid_t uuid, void *value, size_t size);
int
dbtree_uv_fetch(daos_handle_t tree, dbtree_probe_opc_t opc, const uuid_t uuid_in, uuid_t uuid_out,
		void *value, size_t size);
int
dbtree_uv_delete(daos_handle_t tree, const uuid_t uuid);
int
dbtree_uv_create_tree(daos_handle_t tree, const uuid_t uuid, unsigned int class, uint64_t feats,
		      unsigned int order, daos_handle_t *tree_new);
int
dbtree_uv_open_tree(daos_handle_t tree, const uuid_t uuid, daos_handle_t *tree_child);
int
dbtree_uv_destroy_tree(daos_handle_t tree, const uuid_t uuid);
int
dbtree_uv_destroy(daos_handle_t tree, const uuid_t uuid);

/* epoch-count: ordered keys */
#define DBTREE_CLASS_EC (DBTREE_DSM_BEGIN + 2)
extern btr_ops_t dbtree_ec_ops;
int
dbtree_ec_update(daos_handle_t tree, uint64_t epoch, const uint64_t *count);
int
dbtree_ec_lookup(daos_handle_t tree, uint64_t epoch, uint64_t *count);
int
dbtree_ec_fetch(daos_handle_t tree, dbtree_probe_opc_t opc, const uint64_t *epoch_in,
		uint64_t *epoch_out, uint64_t *count);
int
dbtree_ec_delete(daos_handle_t tree, uint64_t epoch);

/**
 * Generic key-value pairs
 *
 * Each key or value is a variable-length byte stream. Keys are ordered by
 * their hash values and must be non-empty.
 */
#define DBTREE_CLASS_KV (DBTREE_DSM_BEGIN + 3)
extern btr_ops_t dbtree_kv_ops;

/**
 * Integer-value pairs
 *
 * Each key is a uint64_t integer. Each value is a variable-length byte stream.
 * Keys are ordered numerically.
 */
#define DBTREE_CLASS_IV (DBTREE_DSM_BEGIN + 4)
extern btr_ops_t dbtree_iv_ops;

/**
 * Using daos_recx_t type direct key, no value.
 */
#define DBTREE_CLASS_RECX (DBTREE_DSM_BEGIN + 5)
extern btr_ops_t dbtree_recx_ops;

/**
 * The key is a uint64_t integer: 32-bits rank + 32-bits VOS tag.
 * The value is the array of DTX IDs.
 * The dbtree is usually in the volatile memory for classifying DTX IDs.
 */
#define DBTREE_CLASS_DTX_CF  (DBTREE_DSM_BEGIN + 6)

/**
 * The key is dtx_cos_key: oid + dkey_hash
 */
#define DBTREE_CLASS_DTX_COS (DBTREE_DSM_BEGIN + 7)

#endif /* __DAOS_SRV_BTREE_CLASS_H__ */
