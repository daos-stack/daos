/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef DAOS_DDB_VOS_H
#define DAOS_DDB_VOS_H

#include <daos_srv/vos_types.h>
#include "ddb_common.h"

struct ddb_cont {
	uuid_t		ddbc_cont_uuid;
	uint32_t	ddbc_idx;
};

struct ddb_obj {
	daos_obj_id_t	ddbo_oid;
	uint32_t	ddbo_idx;
};

struct ddb_key {
	daos_key_t	ddbk_key;
	uint32_t	ddbk_idx;
};

struct ddb_sv {
	uint64_t	ddbs_record_size;
	uint32_t	ddbs_idx;
};

struct ddb_array {
	uint64_t	ddba_record_size;
	daos_recx_t	ddba_recx;
	uint32_t	ddba_idx;
};

/* Open and close a pool for a ddb_ctx */
int ddb_vos_pool_open(char *path, daos_handle_t *poh);
int ddb_vos_pool_close(daos_handle_t poh);

/* Open and close a cont for a ddb_ctx */
int dv_cont_open(daos_handle_t poh, unsigned char *uuid, daos_handle_t *coh);
int dv_cont_close(daos_handle_t *coh);

/*
 * Table of functions for handling parts of a vos tree. Is used with vos_iterate and well defined
 * structures for each tree branch.
 */
struct vos_tree_handlers {
	int (*ddb_cont_handler)(struct ddb_cont *cont, void *args);
	int (*ddb_obj_handler)(struct ddb_obj *obj, void *args);
	int (*ddb_dkey_handler)(struct ddb_key *key, void *args);
	int (*ddb_akey_handler)(struct ddb_key *key, void *args);
	int (*ddb_sv_handler)(struct ddb_sv *key, void *args);
	int (*ddb_array_handler)(struct ddb_array *key, void *args);
};

/*
 * Traverse over a vos tree. The starting point is indicated by the path passed.
 */
/**
 *
 * @param poh		Open pool handle
 * @param path		Starting point for traversing the tree
 * @param recursive	Whether to traverse the tree from the starting path recursively, or
 *			just the immediate children
 * @param handlers	Function table providing the callbacks for handling the vos tree parts
 * @param handler_args	arguments to the handlers
 * @return		0 if success, else error
 */
int dv_iterate(daos_handle_t poh, struct dv_tree_path *path, bool recursive,
	       struct vos_tree_handlers *handlers, void *handler_args);


/* The following functions lookup a vos path part given a starting point and the index desired */
int dv_get_cont_uuid(daos_handle_t poh, uint32_t idx, uuid_t uuid);
int dv_get_object_oid(daos_handle_t coh, uint32_t idx, daos_unit_oid_t *uoid);
int dv_get_dkey(daos_handle_t coh, daos_unit_oid_t uoid, uint32_t idx, daos_key_t *dkey);
int dv_get_akey(daos_handle_t coh, daos_unit_oid_t uoid, daos_key_t *dkey, uint32_t idx,
		daos_key_t *akey);
int dv_get_recx(daos_handle_t coh, daos_unit_oid_t uoid, daos_key_t *dkey, daos_key_t *akey,
		uint32_t idx, daos_recx_t *recx);

/**
 * Update the tree path within the builder with any indexes that the builder was set with.
 * @param ctx		application context
 * @param vt_path	The path builder structure.
 * @return		0 if successful, else error.
 */
int dv_path_update_from_indexes(struct dv_tree_path_builder *vt_path);

#endif /* DAOS_DDB_VOS_H */
