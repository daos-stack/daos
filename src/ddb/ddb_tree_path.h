/**
 * (C) Copyright 2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef DAOS_DDB_TREE_PATH_H
#define DAOS_DDB_TREE_PATH_H

#include "ddb_common.h"

#define DF_DDB_RECX	"{"DF_U64"-"DF_U64"}"
#define DP_DDB_RECX(r)	(r).rx_idx, ((r).rx_idx + (r).rx_nr - 1)

#define INVALID_IDX          (-1)
#define INVALID_PATH "INVALID PATH"
#define DDB_MAX_PRITABLE_KEY 1024

#define ERROR_BASE 5000
enum ddb_parse_error {
	DDBER_INVALID_UNKNOWN       = ERROR_BASE + 0,
	DDBER_INVALID_CONT          = ERROR_BASE + 1,
	DDBER_INVALID_OBJ           = ERROR_BASE + 2,
	DDBER_INVALID_DKEY          = ERROR_BASE + 3,
	DDBER_INVALID_AKEY          = ERROR_BASE + 4,
	DDBER_INVALID_RECX          = ERROR_BASE + 5,
	DDBER_INCOMPLETE_PATH_VALUE = ERROR_BASE + 6,
};

int itp_handle_path_parse_error(struct ddb_ctx *ctx, int rc);

enum path_parts {
	PATH_PART_CONT = 0,
	PATH_PART_OBJ  = 1,
	PATH_PART_DKEY = 2,
	PATH_PART_AKEY = 3,
	PATH_PART_RECX = 4,
	PATH_PART_SV   = 5,
	PATH_PART_END  = 6
};

/*
 * VOS paths have multiple parts (container, object, dkey, akey, recx) and each part has 2 pieces,
 * its 'value' (i.e. container uuid, object id, etc) and an index. The indexed_tree_path_part
 * structure stores the part's value and index, while the dv_indexed_tree_path structure contains
 * all path parts for a VOS path.
 */
struct indexed_tree_path_part {
	union itp_part_type {
		uuid_t		itp_uuid;
		daos_unit_oid_t itp_oid;
		daos_key_t	itp_key; /* akey or dkey */
		daos_recx_t	itp_recx;
	}		itp_part_value;
	uint32_t        itp_part_idx;
	bool            itp_has_part_idx;
	bool            itp_has_part_value;
};

struct dv_indexed_tree_path {
	struct indexed_tree_path_part	itp_parts[PATH_PART_END];
	enum path_parts                 itp_child_type;
};

/**
 * Parse string input to a structured path directing to a given node in a VOS tree. The format of
 * the path should be VOS path parts separated by a forward slash ('/'), starting with a
 * container to the depth desired. The path parts can be the unique identifier for the part or an
 * index (as provided by the list command). Path parts include:
 * Container:	full uuid (uuid_t a formatted by DF_UUIDF)
 * Object Id:	full unit object ID (daos_unit_oid_t as formatted by DF_UOID)
 * D Key:	string representation of the key
 * A Key:	string representation of the key
 * RECX:	start idx - end idx. This is different than how recx might be printed in
 *		log files (using DF_RECX format). Instead will use DF_DDB_RECX and look like:
 *		{lo-hi}
 * @param path		input path
 * @param itp		output structure path
 * @return		0 if success, else error
 */
int itp_parse(const char *path, struct dv_indexed_tree_path *itp);

/* Deep copy of the path */
void itp_copy(struct dv_indexed_tree_path *src, struct dv_indexed_tree_path *dst);

/* Free any memory that was allocated for the path structures */
void itp_free(struct dv_indexed_tree_path *itp);

/* Generic functions for setting the path parts */
bool
itp_part_value_set(struct dv_indexed_tree_path *itp, enum path_parts part_key, void *part_value);
bool
     itp_idx_set(struct dv_indexed_tree_path *itp, enum path_parts part_key, uint32_t idx);

/* Functions for setting parts as a specific path part (i.e. container, object, ... */
bool itp_part_set_cont(union itp_part_type *part, void *part_value);
bool itp_part_set_obj(union itp_part_type *part, void *part_value);
bool itp_part_set_key(union itp_part_type *part, void *part_value);
bool itp_part_set_recx(union itp_part_type *part, void *part_value);

/* Functions for setting the parts (cont, obj, ...) of a indexed tree path */
bool itp_set_cont(struct dv_indexed_tree_path *itp, uuid_t cont_uuid, uint32_t idx);
bool itp_set_cont_idx(struct dv_indexed_tree_path *itp, uint32_t idx);
bool itp_set_cont_part_value(struct dv_indexed_tree_path *itp, unsigned char *cont_uuid);
bool itp_set_obj(struct dv_indexed_tree_path *itp, daos_unit_oid_t oid, uint32_t idx);
bool itp_set_obj_part_value(struct dv_indexed_tree_path *itp, daos_unit_oid_t oid);
bool itp_set_dkey(struct dv_indexed_tree_path *itp, daos_key_t *key, uint32_t idx);
bool itp_set_dkey_part_value(struct dv_indexed_tree_path *itp, daos_key_t *key);
bool itp_set_akey(struct dv_indexed_tree_path *itp, daos_key_t *key, uint32_t idx);
bool itp_set_akey_part_value(struct dv_indexed_tree_path *itp, daos_key_t *key);
bool itp_set_recx(struct dv_indexed_tree_path *itp, daos_recx_t *recx, uint32_t idx);
bool itp_set_recx_part_value(struct dv_indexed_tree_path *itp, daos_recx_t *recx);

void itp_unset_recx(struct dv_indexed_tree_path *itp);
void itp_unset_akey(struct dv_indexed_tree_path *itp);
void itp_unset_dkey(struct dv_indexed_tree_path *itp);
void itp_unset_obj(struct dv_indexed_tree_path *itp);
void itp_unset_cont(struct dv_indexed_tree_path *itp);

/* Get the part's index */
int
itp_idx(struct dv_indexed_tree_path *itp, enum path_parts part_key);

/* path part has both index and part_value */
bool
itp_has_complete(struct dv_indexed_tree_path *itp, enum path_parts part_key);

/* path part has either index or part_value */
bool
itp_has(struct dv_indexed_tree_path *itp, enum path_parts part_key);

/* path part has an index */
bool
itp_has_idx(struct dv_indexed_tree_path *itp, enum path_parts part_key);

/* path part has a part value */
bool
		 itp_has_part_value(struct dv_indexed_tree_path *itp, enum path_parts part_key);

/* Have specific complete part or partial part */
bool itp_has_cont_complete(struct dv_indexed_tree_path *itp);
bool itp_has_cont(struct dv_indexed_tree_path *itp);
bool itp_has_obj_complete(struct dv_indexed_tree_path *itp);
bool itp_has_obj(struct dv_indexed_tree_path *itp);
bool itp_has_dkey_complete(struct dv_indexed_tree_path *itp);
bool itp_has_dkey(struct dv_indexed_tree_path *itp);
bool itp_has_akey_complete(struct dv_indexed_tree_path *itp);
bool itp_has_akey(struct dv_indexed_tree_path *itp);
bool itp_has_recx_complete(struct dv_indexed_tree_path *itp);
bool itp_has_recx(struct dv_indexed_tree_path *itp);
int itp_verify(struct dv_indexed_tree_path *itp);

/* path is complete to a value (array or single value) */
bool itp_has_value(struct dv_indexed_tree_path *itp);

/* Functions for getting specific parts' part_values */
uint8_t *itp_cont(struct dv_indexed_tree_path *itp);
daos_unit_oid_t *itp_oid(struct dv_indexed_tree_path *itp);
daos_key_t *itp_dkey(struct dv_indexed_tree_path *itp);
daos_key_t *itp_akey(struct dv_indexed_tree_path *itp);
daos_recx_t *itp_recx(struct dv_indexed_tree_path *itp);

/* Functions for getting specific parts' index */
int itp_cont_idx(struct dv_indexed_tree_path *itp);
int itp_obj_idx(struct dv_indexed_tree_path *itp);
int itp_dkey_idx(struct dv_indexed_tree_path *itp);
int itp_akey_idx(struct dv_indexed_tree_path *itp);
int itp_recx_idx(struct dv_indexed_tree_path *itp);

/* Printing functions */
void itp_print_indexes(struct ddb_ctx *ctx, struct dv_indexed_tree_path *itp);
void itp_print_parts(struct ddb_ctx *ctx, struct dv_indexed_tree_path *itp);
void itp_print_full(struct ddb_ctx *ctx, struct dv_indexed_tree_path *itp);
void itp_print_part_key(struct ddb_ctx *ctx, union itp_part_type *key_part);

/*
 * This function is used when printing keys. It checks each character in the buffer and will
 * prepend the escape character ('\') before special characters (example '{', '}', ...). This way,
 * printed keys can be used (copy/pasted) directly in VOS paths.
 */
bool itp_key_safe_str(char *buf, size_t buf_len);

/*
 * Tree Path. Simplified version of itp
 */
struct dv_tree_path {
	uuid_t		vtp_cont;
	daos_unit_oid_t vtp_oid;
	daos_key_t	vtp_dkey;
	daos_key_t	vtp_akey;
	daos_recx_t	vtp_recx;
	bool		vtp_is_recx;
};

void itp_to_vos_path(struct dv_indexed_tree_path *itp, struct dv_tree_path *result);
bool dv_has_cont(struct dv_tree_path *vtp);
bool dv_has_obj(struct dv_tree_path *vtp);
bool dv_has_dkey(struct dv_tree_path *vtp);
bool dv_has_akey(struct dv_tree_path *vtp);
bool dv_has_recx(struct dv_tree_path *vtp);
bool dvp_is_complete(struct dv_tree_path *vtp);
bool dvp_is_empty(struct dv_tree_path *vtp);

#endif /* DAOS_DDB_TREE_PATH_H */
