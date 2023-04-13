/**
 * (C) Copyright 2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "ddb_tree_path.h"
#include "ddb_printer.h"
#include "ddb_parse.h"

/*
 * ------------------------------------------------------
 * Functions for parsing a path into the structure
 * ------------------------------------------------------
 */

/* this should not be used for parsing string keys ... doesn't check for escaped chars */
static int
str_part_len(const char *p)
{
	int i = 0;

	while (p[i] != '\0' && p[i] != '/')
		i++;

	return i;
}

/* parse the string to a bracketed index "[123]" */
static inline int
try_parse_idx(const char *str, uint32_t *idx)
{
	uint32_t str_len;

	D_ASSERT(str);
	str_len = str_part_len(str);

	if (str_len < 3) /* must be at least 3 chars */
		return -DER_INVAL;

	if (str[0] == '[' && str[str_len - 1] == ']') {
		*idx = atol(str + 1);
		return str_len;
	}
	return -DER_INVAL;
}
static inline bool
is_idx_set(char *str, struct indexed_tree_path_part *part)
{
	uint32_t str_len;

	D_ASSERT(str);
	str_len = strlen(str);

	if (str_len < 3) /* must be at least 3 chars */
		return false;

	if (str[0] == '[' && str[str_len - 1] == ']') {
		part->itp_idx = atol(str + 1);
		part->itp_has_idx = true;
		return true;
	}
	return false;
}

int
parse_cont(const char *cont, struct dv_indexed_tree_path *itp)
{
	uuid_t cont_uuid;
	char cont_uuid_str[DAOS_UUID_STR_SIZE] = {0};
	int rc;


	if (str_part_len(cont) == 0)
		return 0;

	if (cont[0] == '[') {
		uint32_t idx;
		rc = try_parse_idx(cont, &idx);
		if (rc < 0)
			return -DDBER_INVALID_CONT;
		itp_set_cont_idx(itp, idx);
		return rc;
	}

	int i;
	for (i = 0; i < min(DAOS_UUID_STR_SIZE, strlen(cont)); ++i) {
		cont_uuid_str[i] = cont[i];
	}
	cont_uuid_str[DAOS_UUID_STR_SIZE - 1] = '\0';
	rc = uuid_parse(cont_uuid_str, cont_uuid);
	if (rc != 0) {
		return -DDBER_INVALID_CONT;
	}
	if (!itp_set_cont_part(itp, cont_uuid)) {
		return -DDBER_INVALID_CONT;
	}

	return DAOS_UUID_STR_SIZE - 1; /* don't include the '\0' */
}

int
parse_oid(const char *oid_str, struct dv_indexed_tree_path *itp)
{
	uint64_t	 oid_parts[4] = {0}; /* 4 parts to the oid */
	const char	*oid_str_idx = oid_str;
	daos_unit_oid_t	 oid;
	int		 i;
	int		 rc;

	if (strlen(oid_str) == 0)
		return 0;

	if (oid_str[0] == '[') {
		uint32_t idx;
		rc = try_parse_idx(oid_str, &idx);
		if (rc < 0)
			return -DDBER_INVALID_OBJ;
		itp_idx_set(itp, PATH_PART_OBJ, idx);
		return rc;
	}

	for (i = 0; i < ARRAY_SIZE(oid_parts); ++i) {
		if (i > 0 && oid_str_idx[0] != '.')
			return -DDBER_INVALID_OBJ;
		if (i > 0)
			oid_str_idx++;
		if (strlen(oid_str_idx) == 0 || oid_str_idx[0] == '/')
			/* found end of oid before expected */
			return -DDBER_INVALID_OBJ;
		oid_parts[i] = atoll(oid_str_idx);
		while(isdigit(oid_str_idx[0])) {
			oid_str_idx++;
		}
	}

	oid.id_pub.hi = oid_parts[0];
	oid.id_pub.lo = oid_parts[1];
	oid.id_shard = oid_parts[2];
	oid.id_layout_ver = oid_parts[3];

	itp_set_obj_part(itp, oid);

	return oid_str_idx - oid_str;
}

int
parse_recx(const char *recx_str, struct dv_indexed_tree_path *itp)
{
	daos_recx_t	 recx = {0};
	const char	*dash;
	const char	*close;
	uint64_t	 lo;
	uint64_t	 hi;
	int		 rc;

	if (strlen(recx_str) == 0)
		return 0;

	if (recx_str[0] == '[') {
		uint32_t idx;

		rc = try_parse_idx(recx_str, &idx);
		if (rc < 0)
			return -DDBER_INVALID_RECX;
		itp_idx_set(itp, PATH_PART_RECX, idx);

		return rc;
	}

	if (recx_str[0] != '{' || recx_str[strlen(recx_str) - 1] != '}')
		return -DDBER_INVALID_RECX;

	dash = recx_str + 1;
	while(isdigit(dash[0]))
		dash++;

	/* found no digits */
	if (dash == recx_str + 1)
		return -DDBER_INVALID_RECX;

	if (dash[0] != '-')
		return -DDBER_INVALID_RECX;

	close = dash + 1;
	while(isdigit(close[0]))
		close++;
	if (close[0] != '}')
		return -DDBER_INVALID_RECX;

	lo = atoll(recx_str + 1);
	hi = atoll(dash + 1);

	recx.rx_idx = lo;
	recx.rx_nr = hi - lo + 1;

	itp_set_recx_part(itp, &recx);

	return strlen(recx_str);
}

static int
parse_key(const char *key_str, struct dv_indexed_tree_path *itp, path_parts_e key_part)
{
	daos_key_t	key = {0};
	int		rc;

	if (strlen(key_str) == 0)
		return 0;

	/* is an index */
	if (key_str[0] == '[') {
		uint32_t idx;
		rc = try_parse_idx(key_str, &idx);
		if (rc < 0)
			return rc;
		itp_idx_set(itp, key_part, idx);
		return rc;
	}

	rc = ddb_parse_key(key_str, &key);
	if (rc < 0)
		return rc;
	
	itp_part_set(itp, key_part, &key);
	daos_iov_free(&key);

	return rc;
}

int
itp_parse(const char *path, struct dv_indexed_tree_path *itp)
{
	const char	*path_idx;
	int		 rc;

	/* Setup vt_path */
	D_ASSERT(itp);
	memset(itp, 0, sizeof(*itp));

	/* If there is no path, leave it empty */
	if (path == NULL || strlen(path) == 0)
		return 0;

	path_idx = path;

	if (path_idx[0] == '/')
		path_idx++;

	rc = parse_cont(path_idx, itp);
	if (rc < 0)
		return rc;
	path_idx += rc;

	if (path_idx[0] == '\0')
		return 0;

	if (path_idx[0] != '/')
		return -DDBER_INVALID_CONT;
	path_idx++;

	/* OID */
	rc = parse_oid(path_idx, itp);
	if (rc < 0)
		return rc;
	path_idx += rc;

	if (path_idx[0] == '\0')
		return 0;

	if (path_idx[0] != '/')
		return -DDBER_INVALID_OBJ;

	/* DKEY */
	path_idx ++;
	d_iov_t key = {0};
	rc = parse_key(path_idx, itp, PATH_PART_DKEY);
	if (rc < 0)
		return -DDBER_INVALID_DKEY;
	itp_set_dkey_part(itp, &key);
	daos_iov_free(&key);
	path_idx += rc;

	if (path_idx[0] == '\0')
		return 0;
	if (path_idx[0] != '/')
		return -DDBER_INVALID_DKEY;

	/* AKEY */
	path_idx ++;
	rc = parse_key(path_idx, itp, PATH_PART_AKEY);
	if (rc < 0)
		return -DDBER_INVALID_AKEY;

	path_idx += rc;

	if (path_idx[0] == '\0')
		return 0;
	if (path_idx[0] != '/')
		return -DDBER_INVALID_AKEY;

	/* RECX */
	path_idx++;
	rc = parse_recx(path_idx, itp);

	if (rc < 0)
		return rc;

	path_idx += rc;

	if (path_idx[0] == '\0')
		return 0;
	if (path_idx[0] != '/')
		return -DDBER_INVALID_RECX;
	path_idx++;
	if (strlen(path_idx) > 0)
		return -DER_INVAL;
	return 0;
}

bool
itp_part_set_cont(union itp_part_type *part, void *part_value)
{
	const uint8_t *cont_uuid = part_value;

	if (cont_uuid == NULL || uuid_is_null(cont_uuid))
		return false;

	uuid_copy(part->itp_uuid, cont_uuid);
	return true;
}

bool
itp_part_set_obj(union itp_part_type *part, void *part_value)
{
	daos_unit_oid_t *oid = part_value;

	if (daos_unit_oid_is_null(*oid))
		return false;

	part->itp_oid = *oid;
	return true;
}

bool
itp_part_set_key(union itp_part_type *part, void *part_value)
{
	daos_key_t *key = part_value;

	if (key->iov_len == 0)
		return false;
	daos_iov_copy(&part->itp_key, key);
	return true;
}

bool
itp_part_set_recx(union itp_part_type *part, void *part_value)
{
	daos_recx_t *recx = part_value;
	if (recx->rx_nr == 0)
		return false;

	part->itp_recx = *recx;
	return true;
}

static bool (*part_set_fn[PATH_PART_END])(union itp_part_type *part, void *part_value) = {
    itp_part_set_cont,
    itp_part_set_obj,
    itp_part_set_key,
    itp_part_set_key,
    itp_part_set_recx,
};

bool
itp_part_set(struct dv_indexed_tree_path *itp, path_parts_e part_key, void *part_value)
{
	struct indexed_tree_path_part *p = &itp->itp_parts[part_key];

	if (part_set_fn[part_key](&p->itp_name, part_value)) {
		p->itp_has_name = true;
		return true;
	}
	return false;
}

bool
itp_idx_set(struct dv_indexed_tree_path *itp, path_parts_e part_key, uint32_t idx)
{
	struct indexed_tree_path_part *p = &itp->itp_parts[part_key];

	if (idx != INVALID_IDX) {
		p->itp_has_idx = true;
		p->itp_idx = idx;

		return true;
	}

	return false;
}

static inline bool
itp_set(struct dv_indexed_tree_path *itp, path_parts_e part_key,void *part_value, uint32_t part_idx)
{
	int i;

	for (i = 0; i < part_key; ++i) {
		if (!(itp->itp_parts[i].itp_has_name && itp->itp_parts[i].itp_has_idx))
			return false;
	}

	return itp_idx_set(itp, part_key, part_idx) && itp_part_set(itp, part_key, part_value);
}

bool
itp_set_cont(struct dv_indexed_tree_path *itp, unsigned char *cont_uuid, uint32_t idx)
{
	return itp_set(itp, PATH_PART_CONT, cont_uuid, idx);
}

bool
itp_set_cont_idx(struct dv_indexed_tree_path *itp, uint32_t idx)
{
	return itp_idx_set(itp, PATH_PART_CONT, idx);
}

bool
itp_set_cont_part(struct dv_indexed_tree_path *itp, unsigned char *cont_uuid)
{
	return itp_part_set(itp, PATH_PART_CONT, cont_uuid);
}

bool
itp_set_obj(struct dv_indexed_tree_path *itp, daos_unit_oid_t oid, uint32_t idx)
{
	return itp_set(itp, PATH_PART_OBJ, &oid, idx);
}

bool
itp_set_obj_part(struct dv_indexed_tree_path *itp, daos_unit_oid_t oid)
{
	return itp_part_set(itp, PATH_PART_OBJ, &oid);
}

bool
itp_set_dkey(struct dv_indexed_tree_path *itp, daos_key_t *key, uint32_t idx)
{
	return itp_set(itp, PATH_PART_DKEY, key, idx);
}

bool
itp_set_dkey_part(struct dv_indexed_tree_path *itp, daos_key_t *key)
{
	return itp_part_set(itp, PATH_PART_DKEY, key);
}

bool
itp_set_akey(struct dv_indexed_tree_path *itp, daos_key_t *key, uint32_t idx)
{
	return itp_set(itp, PATH_PART_AKEY, key, idx);
}

bool
itp_set_akey_part(struct dv_indexed_tree_path *itp, daos_key_t *key)
{
	return itp_part_set(itp, PATH_PART_AKEY, key);
}

bool
itp_set_recx(struct dv_indexed_tree_path *itp, daos_recx_t *recx, uint32_t idx)
{
	return itp_set(itp, PATH_PART_RECX, recx, idx);
}

bool /* [todo-ryon]: should this be set recx_name?? or value ... part means the whole part */
itp_set_recx_part(struct dv_indexed_tree_path *itp, daos_recx_t *recx)
{
	return itp_part_set(itp, PATH_PART_RECX, recx);
}

/*
 * ---------------------------------------------------
 * Functions for printing the path
 * ---------------------------------------------------
 */

static inline void
itp_print_part_cont(struct ddb_ctx *ctx, union itp_part_type *v)
{
	ddb_printf(ctx, DF_UUIDF, DP_UUID(v->itp_uuid));
}

static inline void
itp_print_part_obj(struct ddb_ctx *ctx, union itp_part_type *v)
{
	ddb_printf(ctx, DF_UOID, DP_UOID(v->itp_oid));
}

void
itp_key_safe_str(char *buf, size_t buf_len)
{
	char tmp[buf_len];
	char *tmp_idx = tmp;
	int  i;
	char escape_chars[] = {
            '/',
            '{',
            '}',
            '\\'
        };

	if (strnlen(buf, buf_len) == 0)
		return;

	for (i = 0; i < strnlen(buf, buf_len); ++i) {
		int  e;
		bool escaped = false;

		for (e = 0; e < ARRAY_SIZE(escape_chars) && !escaped; ++e) {
			if (buf[i] == escape_chars[e]) {
				sprintf(tmp_idx, "\\%c", buf[i]);
				tmp_idx += 2;
				escaped = true;
			}
		}
		if  (!escaped){
			sprintf(tmp_idx, "%c", buf[i]);
			tmp_idx++;
		}
	}
	strncpy(buf, tmp, buf_len);
}


void
itp_print_part_key(struct ddb_ctx *ctx, union itp_part_type *v)
{
	char buf[1024];

	ddb_iov_to_printable_buf(&v->itp_key, buf, ARRAY_SIZE(buf));
	if (ddb_can_print(&v->itp_key)) {
		/* print the size with the string key if the size isn't strlen. That way
		 * parsing the string into a valid key will work
		 */
		itp_key_safe_str(buf, ARRAY_SIZE(buf));
		if (v->itp_key.iov_len != strlen((char*)v->itp_key.iov_buf))
			ddb_printf(ctx, "%s{%lu}", buf, v->itp_key.iov_len);
		else
			ddb_printf(ctx, "%s", buf);
	} else {
		/* is an int or binary and already formatted in iov_to_pritable_buf */
		ddb_printf(ctx, "{%s}", buf);
	}
}

static inline void
itp_print_part_recx(struct ddb_ctx *ctx, union itp_part_type *v)
{
	ddb_printf(ctx, DF_DDB_RECX, DP_DDB_RECX(v->itp_recx));
}

static void (*print_fn[PATH_PART_END])(struct ddb_ctx *ctx, union itp_part_type *v) = {
    itp_print_part_cont,
    itp_print_part_obj,
    itp_print_part_key,
    itp_print_part_key,
    itp_print_part_recx,
};

void
itp_print_parts(struct ddb_ctx *ctx, struct dv_indexed_tree_path *itp)
{
	int i;

	if (!itp->itp_parts[PATH_PART_CONT].itp_has_name) {
		ddb_print(ctx, "/");
		return;
	}

	for (i = 0; i < PATH_PART_END; i++) {
		if (!itp->itp_parts[i].itp_has_name)
			break;
		ddb_print(ctx, "/");
		print_fn[i](ctx, &itp->itp_parts[i].itp_name);
	}
}

void
itp_print_indexes(struct ddb_ctx *ctx, struct dv_indexed_tree_path *itp)
{
	int i;

	for (i = 0; i < PATH_PART_END; i++) {
		if (!itp->itp_parts[i].itp_has_idx)
			return;
		ddb_printf(ctx, "/"DF_IDX, DP_IDX(itp->itp_parts[i].itp_idx));
	}
}

void
itp_print_full(struct ddb_ctx *ctx, struct dv_indexed_tree_path *itp)
{
	char	part_name[][PATH_PART_END] = {
	    "CONT",
	    "OBJ",
	    "DKEY",
	    "AKEY",
	    "RECX"
	};
	int	i;
	int	part_set = -1;

	D_ASSERT(itp != NULL);

	for (i = 0; i < PATH_PART_END; ++i) {
		if ((itp->itp_parts[i].itp_has_idx != itp->itp_parts[i].itp_has_name)) {
			ddb_print(ctx, INVALID_PATH);
			return;
		} else if (itp->itp_parts[i].itp_has_idx) {
			part_set++;
		}
	}

	/* nothing in path */
	if (part_set == -1) {
		ddb_print(ctx, "/");
		return;
	}

	ddb_printf(ctx, "%s: ", part_name[part_set]);
	ddb_print(ctx, "(");
	itp_print_indexes(ctx, itp);
	ddb_print(ctx, ") ");

	itp_print_parts(ctx, itp);
}

/* If any memory was allocated for the path structure, free it */
void
itp_free(struct dv_indexed_tree_path *itp)
{
	if (itp->itp_parts[PATH_PART_DKEY].itp_has_name)
		daos_iov_free(&itp->itp_parts[PATH_PART_DKEY].itp_name.itp_key);
	if (itp->itp_parts[PATH_PART_AKEY].itp_has_name)
		daos_iov_free(&itp->itp_parts[PATH_PART_AKEY].itp_name.itp_key);

	memset(itp, 0, sizeof(*itp));
}
