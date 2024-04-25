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
int
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

int
parse_cont(const char *cont, struct dv_indexed_tree_path *itp)
{
	uuid_t	cont_uuid;
	char	cont_uuid_str[DAOS_UUID_STR_SIZE] = {0};
	int	rc;
	int	i;

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

	for (i = 0; i < min(DAOS_UUID_STR_SIZE, strlen(cont)); ++i)
		cont_uuid_str[i] = cont[i];
	cont_uuid_str[DAOS_UUID_STR_SIZE - 1] = '\0';
	rc = uuid_parse(cont_uuid_str, cont_uuid);
	if (rc != 0)
		return -DDBER_INVALID_CONT;
	if (!itp_set_cont_part_value(itp, cont_uuid))
		return -DDBER_INVALID_CONT;

	return DAOS_UUID_STR_SIZE - 1; /* don't include the '\0' */
}

int
parse_oid(const char *oid_str, struct dv_indexed_tree_path *itp)
{
	uint64_t	 oid_parts[4] = {0}; /* 4 parts to the oid */
	const char	*oid_str_idx = oid_str;
	daos_unit_oid_t	 oid = {0};
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
		while (isdigit(oid_str_idx[0]))
			oid_str_idx++;
	}

	oid.id_pub.hi = oid_parts[0];
	oid.id_pub.lo = oid_parts[1];
	oid.id_shard = oid_parts[2];
	oid.id_layout_ver = oid_parts[3];

	itp_set_obj_part_value(itp, oid);

	return (int)(oid_str_idx - oid_str);
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
	while (isdigit(dash[0]))
		dash++;

	/* found no digits */
	if (dash == recx_str + 1)
		return -DDBER_INVALID_RECX;

	if (dash[0] != '-')
		return -DDBER_INVALID_RECX;

	close = dash + 1;
	while (isdigit(close[0]))
		close++;
	if (close[0] != '}')
		return -DDBER_INVALID_RECX;

	lo = atoll(recx_str + 1);
	hi = atoll(dash + 1);

	recx.rx_idx = lo;
	recx.rx_nr = hi - lo + 1;

	itp_set_recx_part_value(itp, &recx);

	return strlen(recx_str);
}

static int
parse_key(const char *key_str, struct dv_indexed_tree_path *itp, enum path_parts key_part)
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

	itp_part_value_set(itp, key_part, &key);
	daos_iov_free(&key);

	return rc;
}

int
itp_parse(const char *path, struct dv_indexed_tree_path *itp)
{
	const char	*path_idx;
	d_iov_t		 key = {0};
	int		 rc;

	/* Setup itp */
	D_ASSERT(itp);
	memset(itp, 0, sizeof(*itp));

	/* If there is no path, leave it empty */
	if (path == NULL || strlen(path) == 0)
		return 0;

	path_idx = path;

	if (path_idx[0] == '/')
		path_idx++;

	/* Container UUID */
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
	path_idx++;
	rc = parse_key(path_idx, itp, PATH_PART_DKEY);
	if (rc < 0)
		return -DDBER_INVALID_DKEY;
	itp_set_dkey_part_value(itp, &key);
	daos_iov_free(&key);
	path_idx += rc;

	if (path_idx[0] == '\0')
		return 0;
	if (path_idx[0] != '/')
		return -DDBER_INVALID_DKEY;

	/* AKEY */
	path_idx++;
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
itp_part_value_set(struct dv_indexed_tree_path *itp, enum path_parts part_key, void *part_value)
{
	struct indexed_tree_path_part *p = &itp->itp_parts[part_key];

	if (part_set_fn[part_key](&p->itp_part_value, part_value)) {
		p->itp_has_part_value = true;
		return true;
	}
	return false;
}

bool
itp_idx_set(struct dv_indexed_tree_path *itp, enum path_parts part_key, uint32_t idx)
{
	struct indexed_tree_path_part *p = &itp->itp_parts[part_key];

	if (idx != INVALID_IDX) {
		p->itp_has_part_idx = true;
		p->itp_part_idx = idx;

		return true;
	}

	return false;
}

static bool
itp_set(struct dv_indexed_tree_path *itp, enum path_parts part_key, void *part_value,
	uint32_t part_idx)
{
	int i;

	/* Make sure everything before this part is already set */
	for (i = 0; i < part_key; ++i) {
		if (!(itp->itp_parts[i].itp_has_part_value && itp->itp_parts[i].itp_has_part_idx))
			return false;
	}

	return itp_idx_set(itp, part_key, part_idx) &&
	       itp_part_value_set(itp, part_key, part_value);
}

bool
itp_set_cont(struct dv_indexed_tree_path *itp, uuid_t cont_uuid, uint32_t idx)
{
	return itp_set(itp, PATH_PART_CONT, cont_uuid, idx);
}

bool
itp_set_cont_idx(struct dv_indexed_tree_path *itp, uint32_t idx)
{
	return itp_idx_set(itp, PATH_PART_CONT, idx);
}

bool
itp_set_cont_part_value(struct dv_indexed_tree_path *itp, unsigned char *cont_uuid)
{
	return itp_part_value_set(itp, PATH_PART_CONT, cont_uuid);
}

bool
itp_set_obj(struct dv_indexed_tree_path *itp, daos_unit_oid_t oid, uint32_t idx)
{
	return itp_set(itp, PATH_PART_OBJ, &oid, idx);
}

bool
itp_set_obj_part_value(struct dv_indexed_tree_path *itp, daos_unit_oid_t oid)
{
	return itp_part_value_set(itp, PATH_PART_OBJ, &oid);
}

bool
itp_set_dkey(struct dv_indexed_tree_path *itp, daos_key_t *key, uint32_t idx)
{
	return itp_set(itp, PATH_PART_DKEY, key, idx);
}

bool
itp_set_dkey_part_value(struct dv_indexed_tree_path *itp, daos_key_t *key)
{
	return itp_part_value_set(itp, PATH_PART_DKEY, key);
}

bool
itp_set_akey(struct dv_indexed_tree_path *itp, daos_key_t *key, uint32_t idx)
{
	return itp_set(itp, PATH_PART_AKEY, key, idx);
}

bool
itp_set_akey_part_value(struct dv_indexed_tree_path *itp, daos_key_t *key)
{
	return itp_part_value_set(itp, PATH_PART_AKEY, key);
}

bool
itp_set_recx(struct dv_indexed_tree_path *itp, daos_recx_t *recx, uint32_t idx)
{
	return itp_set(itp, PATH_PART_RECX, recx, idx);
}

bool
itp_set_recx_part_value(struct dv_indexed_tree_path *itp, daos_recx_t *recx)
{
	return itp_part_value_set(itp, PATH_PART_RECX, recx);
}

void
unset_path_part(struct indexed_tree_path_part *part)
{
	part->itp_has_part_value = part->itp_has_part_idx = false;
	memset(&part->itp_part_value, 0, sizeof(part->itp_part_value));
}

void
itp_unset_recx(struct dv_indexed_tree_path *itp)
{
	unset_path_part(&itp->itp_parts[PATH_PART_RECX]);
}


void
itp_unset_akey(struct dv_indexed_tree_path *itp)
{
	if (itp->itp_parts[PATH_PART_AKEY].itp_has_part_value)
		daos_iov_free(&itp->itp_parts[PATH_PART_AKEY].itp_part_value.itp_key);
	unset_path_part(&itp->itp_parts[PATH_PART_AKEY]);
	itp_unset_recx(itp);
}

void
itp_unset_dkey(struct dv_indexed_tree_path *itp)
{
	if (itp->itp_parts[PATH_PART_DKEY].itp_has_part_value)
		daos_iov_free(&itp->itp_parts[PATH_PART_DKEY].itp_part_value.itp_key);
	unset_path_part(&itp->itp_parts[PATH_PART_DKEY]);
	itp_unset_akey(itp);
}

void
itp_unset_obj(struct dv_indexed_tree_path *itp)
{
	unset_path_part(&itp->itp_parts[PATH_PART_OBJ]);
	itp_unset_dkey(itp);
}

void
itp_unset_cont(struct dv_indexed_tree_path *itp)
{
	unset_path_part(&itp->itp_parts[PATH_PART_CONT]);
	itp_unset_obj(itp);
}

int
itp_idx(struct dv_indexed_tree_path *itp, enum path_parts part_key)
{
	return itp->itp_parts[part_key].itp_part_idx;
}

bool
itp_has_complete(struct dv_indexed_tree_path *itp, enum path_parts part_key)
{
	return itp->itp_parts[part_key].itp_has_part_value &&
	       itp->itp_parts[part_key].itp_has_part_idx;
}

bool
itp_has(struct dv_indexed_tree_path *itp, enum path_parts part_key)
{
	return itp->itp_parts[part_key].itp_has_part_value ||
	       itp->itp_parts[part_key].itp_has_part_idx;
}

bool
itp_has_value(struct dv_indexed_tree_path *itp)
{
	return itp_has(itp, PATH_PART_RECX) ||
	       (itp_has(itp, PATH_PART_AKEY) && itp->itp_child_type == PATH_PART_SV);
}

bool
itp_has_idx(struct dv_indexed_tree_path *itp, enum path_parts part_key)
{
	return itp->itp_parts[part_key].itp_has_part_idx;
}

bool
itp_has_part_value(struct dv_indexed_tree_path *itp, enum path_parts part_key)
{
	return itp->itp_parts[part_key].itp_has_part_value;
}

bool
itp_has_cont_complete(struct dv_indexed_tree_path *itp)
{
	return itp_has_complete(itp, PATH_PART_CONT);
}

bool
itp_has_cont(struct dv_indexed_tree_path *itp)
{
	return itp_has(itp, PATH_PART_CONT);
}

bool
itp_has_obj_complete(struct dv_indexed_tree_path *itp)
{
	return itp_has_complete(itp, PATH_PART_OBJ);
}

bool
itp_has_obj(struct dv_indexed_tree_path *itp)
{
	return itp_has(itp, PATH_PART_OBJ);
}

bool
itp_has_dkey_complete(struct dv_indexed_tree_path *itp)
{
	return itp_has_complete(itp, PATH_PART_DKEY);
}

bool
itp_has_dkey(struct dv_indexed_tree_path *itp)
{
	return itp_has(itp, PATH_PART_DKEY);
}

bool
itp_has_akey_complete(struct dv_indexed_tree_path *itp)
{
	return itp_has_complete(itp, PATH_PART_AKEY);
}

bool
itp_has_akey(struct dv_indexed_tree_path *itp)
{
	return itp_has(itp, PATH_PART_AKEY);
}

bool
itp_has_recx_complete(struct dv_indexed_tree_path *itp)
{
	return itp_has_complete(itp, PATH_PART_RECX);
}

bool
itp_has_recx(struct dv_indexed_tree_path *itp)
{
	return itp_has(itp, PATH_PART_RECX);
}

int
itp_verify(struct dv_indexed_tree_path *itp)
{
	enum path_parts	i;
	uint32_t	path_part_to_error[] = {
	    /* Must match the ordering of enum path_parts */
	    DDBER_INVALID_CONT,
	    DDBER_INVALID_OBJ,
	    DDBER_INVALID_DKEY,
	    DDBER_INVALID_AKEY,
	    DDBER_INVALID_RECX,
	};

	for (i = PATH_PART_CONT; i < PATH_PART_END - 1; ++i) { /* -1 because SV not included */
		if (itp->itp_parts[i].itp_has_part_idx != itp->itp_parts[i].itp_has_part_value)
			return -path_part_to_error[i];
	}

	return 0;
}

/* Functions for getting parts */
static union itp_part_type *
itp_value(struct dv_indexed_tree_path *itp, enum path_parts path_key)
{
	return &itp->itp_parts[path_key].itp_part_value;
}

uint8_t *
itp_cont(struct dv_indexed_tree_path *itp)
{
	return itp_value(itp, PATH_PART_CONT)->itp_uuid;
}

daos_unit_oid_t *
itp_oid(struct dv_indexed_tree_path *itp)
{
	return &itp_value(itp, PATH_PART_OBJ)->itp_oid;
}

daos_key_t *
itp_dkey(struct dv_indexed_tree_path *itp)
{
	return &itp_value(itp, PATH_PART_DKEY)->itp_key;
}

daos_key_t *
itp_akey(struct dv_indexed_tree_path *itp)
{
	return &itp_value(itp, PATH_PART_AKEY)->itp_key;
}

daos_recx_t *
itp_recx(struct dv_indexed_tree_path *itp)
{
	return &itp_value(itp, PATH_PART_RECX)->itp_recx;
}

int
itp_cont_idx(struct dv_indexed_tree_path *itp)
{
	return itp_idx(itp, PATH_PART_CONT);
}

int
itp_obj_idx(struct dv_indexed_tree_path *itp)
{
	return itp_idx(itp, PATH_PART_OBJ);
}

int
itp_dkey_idx(struct dv_indexed_tree_path *itp)
{
	return itp_idx(itp, PATH_PART_DKEY);
}

int
itp_akey_idx(struct dv_indexed_tree_path *itp)
{
	return itp_idx(itp, PATH_PART_AKEY);
}

int
itp_recx_idx(struct dv_indexed_tree_path *itp)
{
	return itp_idx(itp, PATH_PART_RECX);
}

/* dv_tree_path Functions */
void
itp_to_vos_path(struct dv_indexed_tree_path *itp, struct dv_tree_path *result)
{
	memset(result, 0, sizeof(*result));

	if (itp_has_part_value(itp, PATH_PART_CONT))
		uuid_copy(result->vtp_cont, itp_cont(itp));
	if (itp_has_part_value(itp, PATH_PART_OBJ))
		result->vtp_oid = *itp_oid(itp);

	if (itp_has_part_value(itp, PATH_PART_DKEY))
		result->vtp_dkey = *itp_dkey(itp);
	if (itp_has_part_value(itp, PATH_PART_AKEY)) {
		result->vtp_is_recx = itp->itp_child_type == PATH_PART_RECX;
		result->vtp_akey = *itp_akey(itp);
	}
	if (itp_has_part_value(itp, PATH_PART_RECX)) {
		result->vtp_recx = *itp_recx(itp);
		result->vtp_is_recx = true;
	}
}

bool
dv_has_cont(struct dv_tree_path *vtp)
{
	return !uuid_is_null(vtp->vtp_cont);
}

bool
dv_has_obj(struct dv_tree_path *vtp)
{
	return !(vtp->vtp_oid.id_pub.lo == 0 &&
		 vtp->vtp_oid.id_pub.hi == 0);
}

bool
dv_has_dkey(struct dv_tree_path *vtp)
{
	return vtp->vtp_dkey.iov_len > 0;
}

bool
dv_has_akey(struct dv_tree_path *vtp)
{
	return vtp->vtp_akey.iov_len > 0;
}

bool
dv_has_recx(struct dv_tree_path *vtp)
{
	return vtp->vtp_recx.rx_nr > 0;
}

bool
dvp_is_complete(struct dv_tree_path *vtp)
{
	return dv_has_cont(vtp) && dv_has_obj(vtp) && dv_has_dkey(vtp) && dv_has_akey(vtp);
}

bool
dvp_is_empty(struct dv_tree_path *vtp)
{
	return !dv_has_cont(vtp) && !dv_has_obj(vtp) && !dv_has_dkey(vtp) && !dv_has_akey(vtp);
}

/*
 * ---------------------------------------------------
 * Functions for printing the path
 * ---------------------------------------------------
 */

void
itp_print_part_cont(struct ddb_ctx *ctx, union itp_part_type *v)
{
	ddb_printf(ctx, DF_UUIDF, DP_UUID(v->itp_uuid));
}

void
itp_print_part_obj(struct ddb_ctx *ctx, union itp_part_type *v)
{
	ddb_printf(ctx, DF_UOID, DP_UOID(v->itp_oid));
}

bool
itp_key_safe_str(char *buf, size_t buf_len)
{
	char tmp[buf_len];
	char *tmp_idx = tmp;
	char *tmp_end = tmp + buf_len - 1;
	int  i;
	char escape_chars[] = { '/', '{', '}', '\\' };

	if (strnlen(buf, buf_len) == 0)
		return true;

	for (i = 0; i < strnlen(buf, buf_len); ++i) {
		int  e;
		bool escaped = false;

		if (tmp_idx + 1 >= tmp_end) { /* +1 for escape character if needed */
			D_ERROR("Buffer was too small to hold the escape characters");
			return false;
		}
		for (e = 0; e < ARRAY_SIZE(escape_chars) && !escaped; ++e) {
			if (buf[i] == escape_chars[e]) {
				sprintf(tmp_idx, "\\%c", buf[i]);
				tmp_idx += 2;
				escaped = true;
			}
		}
		if (!escaped) {
			sprintf(tmp_idx, "%c", buf[i]);
			tmp_idx++;
		}
	}
	strncpy(buf, tmp, buf_len);

	return true;
}

void
itp_print_part_key(struct ddb_ctx *ctx, union itp_part_type *key_part)
{
	char	buf[DDB_MAX_PRITABLE_KEY];
	d_iov_t *key_iov = &key_part->itp_key;

	ddb_iov_to_printable_buf(key_iov, buf, ARRAY_SIZE(buf));
	if (ddb_can_print(key_iov)) {
		/* +1 to make sure there's room for a null terminator */
		char key_str[key_part->itp_key.iov_len + 1];

		memcpy(key_str, key_iov->iov_buf, key_iov->iov_len);
		key_str[key_iov->iov_len] = '\0';
		/* buffer should be plenty big, but just in case ... */
		if (!itp_key_safe_str(buf, ARRAY_SIZE(buf))) {
			ddb_print(ctx, "(ISSUE PRINTING KEY)");
			return;
		}
		/* print the size with the string key if the size isn't strlen. That way
		 * parsing the string into a valid key will work
		 */
		if (key_iov->iov_len != strlen(key_str))
			ddb_printf(ctx, "%s{%lu}", buf, key_iov->iov_len);
		else
			ddb_printf(ctx, "%s", buf);
	} else {
		/* is an int or binary and already formatted in iov_to_pritable_buf */
		ddb_printf(ctx, "{%s}", buf);
	}
}

void
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

	if (!itp->itp_parts[PATH_PART_CONT].itp_has_part_value) {
		ddb_print(ctx, "/");
		return;
	}

	for (i = 0; i < PATH_PART_END; i++) {
		if (!itp->itp_parts[i].itp_has_part_value)
			break;
		ddb_print(ctx, "/");
		print_fn[i](ctx, &itp->itp_parts[i].itp_part_value);
	}
}

void
itp_print_indexes(struct ddb_ctx *ctx, struct dv_indexed_tree_path *itp)
{
	int i;

	for (i = 0; i < PATH_PART_END; i++) {
		if (!itp->itp_parts[i].itp_has_part_idx)
			return;
		ddb_printf(ctx, "/"DF_IDX, DP_IDX(itp->itp_parts[i].itp_part_idx));
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
		if (itp->itp_parts[i].itp_has_part_idx != itp->itp_parts[i].itp_has_part_value) {
			ddb_print(ctx, INVALID_PATH);
			return;
		} else if (itp->itp_parts[i].itp_has_part_idx) {
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

void
itp_copy(struct dv_indexed_tree_path *src, struct dv_indexed_tree_path *dst)
{
	if (src == NULL || dst == NULL)
		return;
	*src = *dst;
	itp_set_cont(dst, itp_cont(src), itp_cont_idx(src));
	itp_set_obj(dst, *itp_oid(src), itp_obj_idx(src));
	itp_set_dkey(dst, itp_dkey(src), itp_dkey_idx(src));
	itp_set_akey(dst, itp_akey(src), itp_akey_idx(src));
	itp_set_recx(dst, itp_recx(src), itp_recx_idx(src));
}

/* If any memory was allocated for the path structure, free it */
void
itp_free(struct dv_indexed_tree_path *itp)
{
	itp_unset_dkey(itp);
	itp_unset_akey(itp);

	memset(itp, 0, sizeof(*itp));
}

static const char * const path_type[] = {
	"",
	"Container",
	"Object",
	"DKEY",
	"AKEY",
	"RECX",
};

int
itp_handle_path_parse_error(struct ddb_ctx *ctx, int rc)
{
	if (!(-rc >= DDBER_INVALID_UNKNOWN && -rc <= DDBER_INCOMPLETE_PATH_VALUE))
		return rc;

	rc = -rc;
	if (rc == DDBER_INVALID_CONT || rc == DDBER_INVALID_OBJ || rc == DDBER_INVALID_DKEY ||
	    rc == DDBER_INVALID_AKEY || rc == DDBER_INVALID_RECX) {

		ddb_printf(ctx, "%s is invalid\n", path_type[rc - ERROR_BASE]);
	} else if (rc == DDBER_INCOMPLETE_PATH_VALUE) {
		ddb_print(ctx, "Incomplete Path. Value needed.\n");
	} else {
		ddb_print(ctx, "Unknown error parsing the path.\n");
	}

	return -DER_INVAL;
}
