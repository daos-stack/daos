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

#define INVALID_IDX -1
#define INVALID_PATH "INVALID PATH"

/* [todo-ryon]: add error -> string function so can be printed better */
typedef enum {
	DDBER_INVALID_UNKNOWN = 0,
	DDBER_INVALID_CONT = 1,
	DDBER_INVALID_OBJ = 2,
	DDBER_INVALID_DKEY = 3,
	DDBER_INVALID_AKEY = 4,
	DDBER_INVALID_RECX = 5,
	DDBER_INCOMPLETE_PATH_VALUE = 6,
} ddb_parse_error_e;

typedef enum  {
	PATH_PART_CONT = 0,
	PATH_PART_OBJ = 1,
	PATH_PART_DKEY = 2,
	PATH_PART_AKEY = 3,
	PATH_PART_RECX = 4,
	PATH_PART_SV = 5,
	PATH_PART_END = 6
} path_parts_e;
/* [todo-ryon]: documentation for what this is and how it works */
struct indexed_tree_path_part {
	union itp_part_type {
		uuid_t itp_uuid;
		daos_unit_oid_t itp_oid;
		daos_key_t itp_key; /* akey or dkey */
		daos_recx_t itp_recx;
	}		itp_name;
	uint32_t	itp_idx;
	bool		itp_has_idx;
	bool		itp_has_name;
};

struct dv_indexed_tree_path {
	struct indexed_tree_path_part	itp_parts[PATH_PART_END];
	path_parts_e			itp_child_type;
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
 * 		log files (using DF_RECX format). Instead will use DF_DDB_RECX and look like:
 * 		{lo-hi}
 * @param path		input path
 * @param itp		output structure path
 * @return		0 if success, else error
 */
int itp_parse(const char *path, struct dv_indexed_tree_path *itp);

/* Generic functions for setting the path parts */
bool itp_part_set(struct dv_indexed_tree_path *itp, path_parts_e part_key, void *part_value);
bool itp_idx_set(struct dv_indexed_tree_path *itp, path_parts_e part_key, uint32_t idx);

/* [todo-ryon]: why are some of these functions inline and some just the declaration  */

/* Functions for setting different parts of the path */
bool itp_part_set_cont(union itp_part_type *part, void *part_value);
bool itp_part_set_obj(union itp_part_type *part, void *part_value);
bool itp_part_set_key(union itp_part_type *part, void *part_value);
bool itp_part_set_recx(union itp_part_type *part, void *part_value);
bool itp_set_cont(struct dv_indexed_tree_path *itp, uuid_t cont_uuid, uint32_t idx);
bool itp_set_cont_idx(struct dv_indexed_tree_path *itp, uint32_t idx);
bool itp_set_cont_part(struct dv_indexed_tree_path *itp, uuid_t cont_uuid);
bool itp_set_obj(struct dv_indexed_tree_path *itp, daos_unit_oid_t oid, uint32_t idx);
bool itp_set_obj_part(struct dv_indexed_tree_path *itp, daos_unit_oid_t oid);
bool itp_set_dkey(struct dv_indexed_tree_path *itp, daos_key_t *key, uint32_t idx);
bool itp_set_dkey_part(struct dv_indexed_tree_path *itp, daos_key_t *key);
bool itp_set_akey(struct dv_indexed_tree_path *itp, daos_key_t *key, uint32_t idx);
bool itp_set_akey_part(struct dv_indexed_tree_path *itp, daos_key_t *key);
bool itp_set_recx(struct dv_indexed_tree_path *itp, daos_recx_t *recx, uint32_t idx);
bool itp_set_recx_part(struct dv_indexed_tree_path *itp, daos_recx_t *recx);

void itp_print_part_key(struct ddb_ctx *ctx, union itp_part_type *v);

static inline void
itp_unset(struct indexed_tree_path_part *part)
{
	part->itp_has_name = part->itp_has_idx = false;
	memset(&part->itp_name, 0, sizeof(part->itp_name));
}

static inline void
itp_unset_recx(struct dv_indexed_tree_path *itp)
{
	itp_unset(&itp->itp_parts[PATH_PART_RECX]);
}


static inline void
itp_unset_akey(struct dv_indexed_tree_path *itp)
{
	if (itp->itp_parts[PATH_PART_AKEY].itp_has_name)
		daos_iov_free(&itp->itp_parts[PATH_PART_AKEY].itp_name.itp_key);
	itp_unset(&itp->itp_parts[PATH_PART_AKEY]);
	itp_unset_recx(itp);
}

static inline void
itp_unset_dkey(struct dv_indexed_tree_path *itp)
{
	if (itp->itp_parts[PATH_PART_DKEY].itp_has_name)
		daos_iov_free(&itp->itp_parts[PATH_PART_DKEY].itp_name.itp_key);
	itp_unset(&itp->itp_parts[PATH_PART_DKEY]);
	itp_unset_akey(itp);
}

static inline void
itp_unset_obj(struct dv_indexed_tree_path *itp)
{
	itp_unset(&itp->itp_parts[PATH_PART_OBJ]);
	itp_unset_dkey(itp);
}

static inline void
itp_unset_cont(struct dv_indexed_tree_path *itp)
{
	itp_unset(&itp->itp_parts[PATH_PART_CONT]);
	itp_unset_obj(itp);
}

static inline int
itp_idx(struct dv_indexed_tree_path *itp, path_parts_e part_key)
{
	return itp->itp_parts[part_key].itp_idx;
}

static inline bool
itp_has_complete(struct dv_indexed_tree_path *itp, path_parts_e part_key)
{
	return itp->itp_parts[part_key].itp_has_name && itp->itp_parts[part_key].itp_has_idx;
}

static inline bool
itp_has(struct dv_indexed_tree_path *itp, path_parts_e part_key)
{
	return itp->itp_parts[part_key].itp_has_name || itp->itp_parts[part_key].itp_has_idx;
}

static inline bool
itp_has_value(struct dv_indexed_tree_path *itp)
{
	return itp_has(itp, PATH_PART_RECX) ||
	       (itp_has(itp, PATH_PART_AKEY) && itp->itp_child_type == PATH_PART_SV);
}

static inline bool
itp_has_idx(struct dv_indexed_tree_path *itp, path_parts_e part_key)
{
	return itp->itp_parts[part_key].itp_has_idx;
}

static inline bool
itp_has_name(struct dv_indexed_tree_path *itp, path_parts_e part_key)
{
	return itp->itp_parts[part_key].itp_has_name;
}

static inline bool
itp_has_cont_complete(struct dv_indexed_tree_path *itp)
{
	return itp_has_complete(itp, PATH_PART_CONT);
}

static inline bool
itp_has_cont(struct dv_indexed_tree_path *itp)
{
	return itp_has(itp, PATH_PART_CONT);
}

static inline bool
itp_has_obj_complete(struct dv_indexed_tree_path *itp)
{
	return itp_has_complete(itp, PATH_PART_OBJ);
}

static inline bool
itp_has_obj(struct dv_indexed_tree_path *itp)
{
	return itp_has(itp, PATH_PART_OBJ);
}

static inline bool
itp_has_dkey_complete(struct dv_indexed_tree_path *itp)
{
	return itp_has_complete(itp, PATH_PART_DKEY);
}

static inline bool
itp_has_dkey(struct dv_indexed_tree_path *itp)
{
	return itp_has(itp, PATH_PART_DKEY);
}
static inline bool
itp_has_akey_complete(struct dv_indexed_tree_path *itp)
{
	return itp_has_complete(itp, PATH_PART_AKEY);
}

static inline bool
itp_has_akey(struct dv_indexed_tree_path *itp)
{
	return itp_has(itp, PATH_PART_AKEY);
}
static inline bool
itp_has_recx_complete(struct dv_indexed_tree_path *itp)
{
	return itp_has_complete(itp, PATH_PART_RECX);
}

static inline bool
itp_has_recx(struct dv_indexed_tree_path *itp)
{
	return itp_has(itp, PATH_PART_RECX);
}

static inline int
itp_verify(struct dv_indexed_tree_path *itp)
{
	path_parts_e i;
	uint32_t path_part_to_error[] = {
	    DDBER_INVALID_CONT,
	    DDBER_INVALID_OBJ,
	    DDBER_INVALID_DKEY,
	    DDBER_INVALID_AKEY,
	    DDBER_INVALID_RECX,
	};

	for (i = PATH_PART_CONT; i < PATH_PART_END; ++i) {
		if (itp->itp_parts[i].itp_has_idx != itp->itp_parts[i].itp_has_name)
			return -path_part_to_error[i];
	}

	return 0;
}

/* Functions for getting parts */

static inline union itp_part_type *
itp_part(struct dv_indexed_tree_path *itp, path_parts_e path_key)
{
	return &itp->itp_parts[path_key].itp_name;
}

static inline uint8_t *
itp_cont(struct dv_indexed_tree_path *itp)
{
	return itp_part(itp, PATH_PART_CONT)->itp_uuid;
}

static inline daos_unit_oid_t *
itp_oid(struct dv_indexed_tree_path *itp)
{
	return &itp_part(itp, PATH_PART_OBJ)->itp_oid;
}

static inline daos_key_t *
itp_dkey(struct dv_indexed_tree_path *itp)
{
	return &itp_part(itp, PATH_PART_DKEY)->itp_key;
}

static inline daos_key_t *
itp_akey(struct dv_indexed_tree_path *itp)
{
	return &itp_part(itp, PATH_PART_AKEY)->itp_key;
}


static inline daos_recx_t *
itp_recx(struct dv_indexed_tree_path *itp)
{
	return &itp_part(itp, PATH_PART_RECX)->itp_recx;
}

/* [todo-ryon]: remove?? */
static inline path_parts_e
itp_path_depth(struct dv_indexed_tree_path *itp)
{
	int i;

	for (i = 0; i < PATH_PART_END; i++) {
		if (!itp->itp_parts[i].itp_has_name) {
			return i - 1;
		}
	}

	return i - 1;
}

/* Printing functions */
void itp_print_indexes(struct ddb_ctx *ctx, struct dv_indexed_tree_path *itp);
void itp_print_parts(struct ddb_ctx *ctx, struct dv_indexed_tree_path *itp);
void itp_print_full(struct ddb_ctx *ctx, struct dv_indexed_tree_path *itp);

/* helper for printing keys */
void itp_key_safe_str(char *buf, size_t buf_len);

/* Free any memory that was allocated for the path structures */
void itp_free(struct dv_indexed_tree_path *itp);

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

static inline void
itp_to_vos_path(struct dv_indexed_tree_path *itp, struct dv_tree_path *result)
{
	memset(result, 0, sizeof(*result));

	if (itp_has_name(itp, PATH_PART_CONT))
		uuid_copy(result->vtp_cont, itp_cont(itp));
	if (itp_has_name(itp, PATH_PART_OBJ))
		result->vtp_oid = *itp_oid(itp);

	if (itp_has_name(itp, PATH_PART_DKEY))
		result->vtp_dkey = *itp_dkey(itp);
	if (itp_has_name(itp, PATH_PART_AKEY)) {
		result->vtp_is_recx = itp->itp_child_type == PATH_PART_RECX;
		result->vtp_akey = *itp_akey(itp);
	}
	if (itp_has_name(itp, PATH_PART_RECX)) {
		result->vtp_recx = *itp_recx(itp);
		result->vtp_is_recx = true;
	}
}

static inline bool
dv_has_cont(struct dv_tree_path *vtp)
{
	return !uuid_is_null(vtp->vtp_cont);
}

static inline bool
dv_has_obj(struct dv_tree_path *vtp)
{
	return !(vtp->vtp_oid.id_pub.lo == 0 &&
		 vtp->vtp_oid.id_pub.hi == 0);
}

static inline bool
dv_has_dkey(struct dv_tree_path *vtp)
{
	return vtp->vtp_dkey.iov_len > 0;
}

static inline bool
dv_has_akey(struct dv_tree_path *vtp)
{
	return vtp->vtp_akey.iov_len > 0;
}

static inline bool
dv_has_recx(struct dv_tree_path *vtp)
{
	return vtp->vtp_recx.rx_nr > 0;
}

static inline bool
dvp_is_complete(struct dv_tree_path *vtp)
{
	return dv_has_cont(vtp) && dv_has_obj(vtp) && dv_has_dkey(vtp) && dv_has_akey(vtp);
}

static inline bool
dvp_is_empty(struct dv_tree_path *vtp)
{
	return !dv_has_cont(vtp) && !dv_has_obj(vtp) && !dv_has_dkey(vtp) && !dv_has_akey(vtp);
}

#endif // DAOS_DDB_TREE_PATH_H
