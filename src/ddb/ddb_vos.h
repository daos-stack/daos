/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef DAOS_DDB_VOS_H
#define DAOS_DDB_VOS_H

#include <vos_layout.h>
#include <daos_srv/vos_types.h>
#include "ddb_common.h"

struct ddb_cont {
	uuid_t		ddbc_cont_uuid;
	uint32_t	ddbc_idx;
};

struct ddb_obj {
	daos_obj_id_t		ddbo_oid;
	uint32_t		ddbo_idx;
	enum daos_otype_t	ddbo_otype;
	char			ddbo_otype_str[32];
	uint32_t		ddbo_nr_grps;
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
int dv_cont_open(daos_handle_t poh, uuid_t uuid, daos_handle_t *coh);
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

struct ddb_superblock {
	uuid_t		dsb_id;
	uint64_t	dsb_cont_nr;
	uint64_t	dsb_nvme_sz;
	uint64_t	dsb_scm_sz;
	uint64_t	dsb_tot_blks; /* vea: Block device capacity */
	uint32_t	dsb_durable_format_version;
	uint32_t	dsb_blk_sz; /* vea: Block size, 4k bytes by default */
	uint32_t	dsb_hdr_blks; /* vea: Reserved blocks for the block device header */
};

typedef int (*dv_dump_superblock_cb)(void *cb_arg, struct ddb_superblock *sb);

int dv_superblock(daos_handle_t poh, dv_dump_superblock_cb cb, void *cb_args);

typedef int (*dv_dump_value_cb)(void *cb_arg, d_iov_t *value);
int dv_dump_value(daos_handle_t poh, struct dv_tree_path *path, dv_dump_value_cb dump_cb,
		  void *cb_arg);

struct ddb_ilog_entry {
	uint32_t	die_idx;
	int32_t		die_status;
	char		die_status_str[32];
	daos_epoch_t	die_epoch;
	uint32_t	die_tx_id;
	uint16_t	die_update_minor_eph;
	uint16_t	die_punch_minor_eph;
};

enum ddb_ilog_op {
	DDB_ILOG_OP_UNKNOWN = 0,
	DDB_ILOG_OP_ABORT = 1,
	DDB_ILOG_OP_PERSIST = 2,
};

typedef int (*dv_dump_ilog_entry)(void *cb_arg, struct ddb_ilog_entry *entry);
int dv_get_obj_ilog_entries(daos_handle_t coh, daos_unit_oid_t oid, dv_dump_ilog_entry cb,
			    void *cb_args);
int dv_process_obj_ilog_entries(daos_handle_t coh, daos_unit_oid_t oid, enum ddb_ilog_op op);

int
dv_get_key_ilog_entries(daos_handle_t coh, daos_unit_oid_t oid, daos_key_t *dkey, daos_key_t *akey,
			dv_dump_ilog_entry cb, void *cb_args);

int dv_process_key_ilog_entries(daos_handle_t coh, daos_unit_oid_t oid, daos_key_t *dkey,
				daos_key_t *akey, enum ddb_ilog_op op);

struct dv_dtx_committed_entry {
	struct dtx_id	ddtx_id;
	daos_epoch_t	ddtx_cmt_time;
	daos_epoch_t	ddtx_epoch;
};

struct dv_dtx_active_entry {
	struct dtx_id	ddtx_id;
	daos_epoch_t	ddtx_handle_time;
	daos_epoch_t	ddtx_epoch;
	uint32_t	ddtx_grp_cnt;
	uint32_t	ddtx_ver;
	uint32_t	ddtx_rec_cnt;
	uint16_t	ddtx_mbs_flags;
	uint16_t	ddtx_flags;
	daos_unit_oid_t ddtx_oid;
};

typedef int (*dv_committed_dtx_handler)(struct dv_dtx_committed_entry *entry, void *cb_arg);
int dv_committed_dtx(daos_handle_t coh, dv_committed_dtx_handler handler_cb, void *handler_arg);
typedef int (*dv_active_dtx_handler)(struct dv_dtx_active_entry *entry, void *cb_arg);
int dv_active_dtx(daos_handle_t coh, dv_active_dtx_handler handler_cb, void *handler_arg);
int dv_clear_committed_table(daos_handle_t coh);

/* Sync the smd table with information saved in blobs */
typedef int (*dv_smd_sync_complete)(void *cb_args, uuid_t pool_id, uint32_t vos_id,
				    uint64_t blob_id, daos_size_t blob_size);
int dv_sync_smd(dv_smd_sync_complete complete_cb, void *cb_args);

int dv_delete(daos_handle_t poh, struct dv_tree_path *vtp);
int dv_update(daos_handle_t poh, struct dv_tree_path *vtp, d_iov_t *iov, daos_epoch_t epoch);

void dv_oid_to_obj(daos_obj_id_t oid, struct ddb_obj *obj);

int ddb_vtp_verify(daos_handle_t poh, struct dv_tree_path *vtp);

#endif /* DAOS_DDB_VOS_H */
