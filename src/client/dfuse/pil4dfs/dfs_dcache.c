/**
 * (C) Copyright 2022-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC DD_FAC(il)

#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <daos/debug.h>
#include <gurt/hash.h>
#include <gurt/list.h>
#include <daos_fs.h>
#include <daos/common.h>

#include "dfs_dcache.h"

/** Size of the hash key prefix */
#define DCACHE_KEY_PREF_SIZE 35
#define DCACHE_KEY_MAX       (DCACHE_KEY_PREF_SIZE - 1 + PATH_MAX)

#ifdef DAOS_BUILD_RELEASE

#define DF_DK          "dk[%zi]"
#define DP_DK(_dk)     strnlen((_dk), DCACHE_KEY_MAX)

#define DF_PATH        "path[%zi]"
#define DP_PATH(_path) strnlen((_path), PATH_MAX)

#else

#define DF_DK          "dk'%s'"
#define DP_DK(_dk)     (_dk)

#define DF_PATH        "path'%s'"
#define DP_PATH(_path) (_path)

#endif

typedef int (*destroy_fn_t)(dfs_dcache_t *);
typedef int (*find_insert_fn_t)(dfs_dcache_t *, char *, size_t, dcache_rec_t **);
typedef void (*drec_incref_fn_t)(dfs_dcache_t *, dcache_rec_t *);
typedef void (*drec_decref_fn_t)(dfs_dcache_t *, dcache_rec_t *);
typedef void (*drec_del_at_fn_t)(dfs_dcache_t *, dcache_rec_t *);
typedef int (*drec_del_fn_t)(dfs_dcache_t *, char *, dcache_rec_t *);

/** DFS directory cache */
struct dfs_dcache {
	/** Cached DAOS file system */
	dfs_t              *dd_dfs;
	/** Hash table holding the cached directories */
	struct d_hash_table dd_dir_hash;
	/** Key prefix of the DFS root directory */
	char                dd_key_root_prefix[DCACHE_KEY_PREF_SIZE];
	/** Destroy a dfs dir-cache */
	destroy_fn_t        destroy_fn;
	/** Return the dir-cahe record of a given location and insert it if needed */
	find_insert_fn_t    find_insert_fn;
	/** Increase the reference counter of a given dir-cache record */
	drec_incref_fn_t    drec_incref_fn;
	/** Decrease the reference counter of a given dir-cache record */
	drec_decref_fn_t    drec_decref_fn;
	/** Delete a given dir-cache record */
	drec_del_at_fn_t    drec_del_at_fn;
	/** Delete the dir-cache record of a given location */
	drec_del_fn_t       drec_del_fn;
};

/** Entry of a DFS directory cache */
struct dcache_rec {
	/** Entry in the hash table of the DFS cache */
	d_list_t         dr_entry;
	/** Cached DFS directory */
	dfs_obj_t       *dr_obj;
	/** Reference counter used to manage memory deallocation */
	_Atomic uint32_t dr_ref;
	/** True iff this entry was deleted */
	_Atomic bool     dr_deleted;
	/** Key prefix used by its child directory */
	char             dr_key_child_prefix[DCACHE_KEY_PREF_SIZE];
	/** Length of the hash key used to compute the hash index */
	size_t           dr_key_len;
	/** the hash key used to compute the hash index */
	char             dr_key[];
};

static inline dcache_rec_t *
dlist2drec(d_list_t *rlink)
{
	return d_list_entry(rlink, dcache_rec_t, dr_entry);
}

static bool
dcache_key_cmp(struct d_hash_table *htable, d_list_t *rlink, const void *key, unsigned int key_len)
{
	dcache_rec_t *rec;

	rec = dlist2drec(rlink);
	if (rec->dr_key_len != key_len)
		return false;
	D_ASSERT(rec->dr_key[key_len] == '\0');
	D_ASSERT(((char *)key)[key_len] == '\0');

	return strncmp(rec->dr_key, (const char *)key, key_len) == 0;
}

static void
dcache_rec_free(struct d_hash_table *htable, d_list_t *rlink)
{
	int           rc;
	dcache_rec_t *rec;

	rec = dlist2drec(rlink);
	D_DEBUG(DB_TRACE, "delete record " DF_DK " (ref=%u)", DP_DK(rec->dr_key),
		atomic_load(&rec->dr_ref));
	rc = dfs_release(rec->dr_obj);
	if (rc)
		DS_ERROR(rc, "dfs_release() failed");
	D_FREE(rec);
}

static void
dcache_rec_addref(struct d_hash_table *htable, d_list_t *rlink)
{
	dcache_rec_t *rec;
	uint32_t      oldref;

	rec    = dlist2drec(rlink);
	oldref = atomic_fetch_add(&rec->dr_ref, 1);
	D_DEBUG(DB_TRACE, "increment ref counter of record " DF_DK " from %u to %u",
		DP_DK(rec->dr_key), oldref, oldref + 1);
}

static bool
dcache_rec_decref(struct d_hash_table *htable, d_list_t *rlink)
{
	dcache_rec_t *rec;
	uint32_t      oldref;

	rec    = dlist2drec(rlink);
	oldref = atomic_fetch_sub(&rec->dr_ref, 1);
	D_DEBUG(DB_TRACE, "decrement ref counter of record " DF_DK " from %u to %u",
		DP_DK(rec->dr_key), oldref, oldref - 1);
	D_ASSERT(oldref >= 1);
	return oldref == 1;
}

static uint32_t
dcache_rec_hash(struct d_hash_table *htable, d_list_t *rlink)
{
	dcache_rec_t *rec;

	rec = dlist2drec(rlink);
	return d_hash_string_u32(rec->dr_key, rec->dr_key_len);
}

static d_hash_table_ops_t dcache_hash_ops = {
    .hop_key_cmp    = dcache_key_cmp,
    .hop_rec_addref = dcache_rec_addref,
    .hop_rec_decref = dcache_rec_decref,
    .hop_rec_free   = dcache_rec_free,
    .hop_rec_hash   = dcache_rec_hash,
};

static int
dcache_destroy_act(dfs_dcache_t *dcache);
static int
dcache_find_insert_act(dfs_dcache_t *dcache, char *path, size_t path_len, dcache_rec_t **rec);
static void
drec_incref_act(dfs_dcache_t *dcache, dcache_rec_t *rec);
static void
drec_decref_act(dfs_dcache_t *dcache, dcache_rec_t *rec);
static void
drec_del_at_act(dfs_dcache_t *dcache, dcache_rec_t *rec);
static int
drec_del_act(dfs_dcache_t *dcache, char *path, dcache_rec_t *parent);

static inline int
dcache_add_root(dfs_dcache_t *dcache, dfs_obj_t *obj)
{
	dcache_rec_t *rec;
	int           rc;

	D_ALLOC(rec, sizeof(*rec) + DCACHE_KEY_PREF_SIZE);
	if (rec == NULL)
		D_GOTO(error, rc = -DER_NOMEM);

	rec->dr_obj = obj;
	atomic_init(&rec->dr_ref, 0);
	atomic_init(&rec->dr_deleted, false);
	memcpy(&rec->dr_key_child_prefix[0], &dcache->dd_key_root_prefix[0], DCACHE_KEY_PREF_SIZE);
	memcpy(&rec->dr_key[0], &dcache->dd_key_root_prefix[0], DCACHE_KEY_PREF_SIZE);
	rec->dr_key_len = DCACHE_KEY_PREF_SIZE - 1;
	rc = d_hash_rec_insert(&dcache->dd_dir_hash, rec->dr_key, rec->dr_key_len, &rec->dr_entry,
			       true);
	if (rc == 0)
		D_GOTO(out, rc = -DER_SUCCESS);

error:
	D_FREE(rec);
out:
	return rc;
}

static int
dcache_create_act(dfs_t *dfs, uint32_t bits, uint32_t rec_timeout, dfs_dcache_t **dcache)
{
	dfs_dcache_t *dcache_tmp = NULL;
	dfs_obj_t    *obj        = NULL;
	daos_obj_id_t obj_id;
	mode_t        mode;
	int           rc;

	D_ALLOC_PTR(dcache_tmp);
	if (dcache_tmp == NULL)
		D_GOTO(error, rc = -DER_NOMEM);

	dcache_tmp->dd_dfs = dfs;

	rc = dfs_lookup(dfs, "/", O_RDWR, &obj, &mode, NULL);
	if (rc != 0)
		D_GOTO(error, rc = daos_errno2der(rc));
	rc = dfs_obj2id(obj, &obj_id);
	D_ASSERT(rc == 0);
	rc = snprintf(&dcache_tmp->dd_key_root_prefix[0], DCACHE_KEY_PREF_SIZE,
		      "%016" PRIx64 "-%016" PRIx64 ":", obj_id.hi, obj_id.lo);
	D_ASSERT(rc == DCACHE_KEY_PREF_SIZE - 1);

	rc = d_hash_table_create_inplace(D_HASH_FT_MUTEX | D_HASH_FT_LRU, bits, NULL,
					 &dcache_hash_ops, &dcache_tmp->dd_dir_hash);
	if (rc != 0)
		D_GOTO(error, rc);

	dcache_tmp->destroy_fn     = dcache_destroy_act;
	dcache_tmp->find_insert_fn = dcache_find_insert_act;
	dcache_tmp->drec_incref_fn = drec_incref_act;
	dcache_tmp->drec_decref_fn = drec_decref_act;
	dcache_tmp->drec_del_at_fn = drec_del_at_act;
	dcache_tmp->drec_del_fn    = drec_del_act;

	rc = dcache_add_root(dcache_tmp, obj);
	if (rc != 0)
		D_GOTO(error, rc);

	*dcache = dcache_tmp;
	D_GOTO(out, rc = -DER_SUCCESS);

error:
	dfs_release(obj);
	D_FREE(dcache_tmp);
out:
	return rc;
}

static int
dcache_destroy_act(dfs_dcache_t *dcache)
{
	d_list_t *rlink = NULL;
	int       rc;

	while ((rlink = d_hash_rec_first(&dcache->dd_dir_hash)) != NULL) {
		bool deleted;

		D_ASSERT(atomic_load(&dlist2drec(rlink)->dr_ref) == 1);
		deleted = d_hash_rec_delete_at(&dcache->dd_dir_hash, rlink);
		D_ASSERT(deleted);
	}

	rc = d_hash_table_destroy_inplace(&dcache->dd_dir_hash, false);
	if (rc != 0) {
		DL_ERROR(rc, "d_hash_table_destroy_inplace() failed");
		D_GOTO(out, rc);
	}

	D_FREE(dcache);

out:
	return rc;
}

static inline dcache_rec_t *
dcache_get(dfs_dcache_t *dcache, const char *key, size_t key_len)
{
	d_list_t *rlink;

	D_ASSERT(dcache != NULL);
	D_ASSERT(key != NULL);
	D_ASSERT(key_len > 0);

	rlink = d_hash_rec_find(&dcache->dd_dir_hash, key, key_len);
	if (rlink == NULL)
		return NULL;
	return dlist2drec(rlink);
}

static inline int
dcache_add(dfs_dcache_t *dcache, dcache_rec_t *parent, const char *name, const char *key,
	   size_t key_len, dcache_rec_t **rec)
{
	dcache_rec_t *rec_tmp = NULL;
	dfs_obj_t    *obj     = NULL;
	daos_obj_id_t obj_id;
	mode_t        mode;
	d_list_t     *rlink;
	int           rc;

	D_ALLOC(rec_tmp, sizeof(*rec_tmp) + key_len + 1);
	if (rec_tmp == NULL)
		D_GOTO(error, rc = -DER_NOMEM);

	atomic_init(&rec_tmp->dr_ref, 1);
	atomic_init(&rec_tmp->dr_deleted, false);

	rc = dfs_lookup_rel(dcache->dd_dfs, parent->dr_obj, name, O_RDWR, &obj, &mode, NULL);
	if (rc != 0)
		D_GOTO(error, rc = daos_errno2der(rc));
	if (!S_ISDIR(mode))
		D_GOTO(error, rc = -DER_NOTDIR);
	rec_tmp->dr_obj = obj;

	rc = dfs_obj2id(obj, &obj_id);
	D_ASSERT(rc == 0);
	rc = snprintf(&rec_tmp->dr_key_child_prefix[0], DCACHE_KEY_PREF_SIZE,
		      "%016" PRIx64 "-%016" PRIx64 ":", obj_id.hi, obj_id.lo);
	D_ASSERT(rc == DCACHE_KEY_PREF_SIZE - 1);

	rec_tmp->dr_key_len = key_len;
	memcpy(rec_tmp->dr_key, key, key_len + 1);

	rlink = d_hash_rec_find_insert(&dcache->dd_dir_hash, rec_tmp->dr_key, key_len,
				       &rec_tmp->dr_entry);
	if (rlink == &rec_tmp->dr_entry) {
		D_DEBUG(DB_TRACE, "add record " DF_DK " with ref counter %u",
			DP_DK(rec_tmp->dr_key), rec_tmp->dr_ref);
	} else {
		dcache_rec_free(&dcache->dd_dir_hash, &rec_tmp->dr_entry);
		rec_tmp = dlist2drec(rlink);
	}

	*rec = rec_tmp;
	D_GOTO(out, rc = -DER_SUCCESS);

error:
	dfs_release(obj);
	D_FREE(rec_tmp);
out:
	return rc;
}

static int
dcache_find_insert_act(dfs_dcache_t *dcache, char *path, size_t path_len, dcache_rec_t **rec)
{
	const size_t  key_prefix_len = DCACHE_KEY_PREF_SIZE - 1;
	dcache_rec_t *rec_tmp;
	dcache_rec_t *parent;
	char         *key;
	char         *key_prefix;
	char         *name;
	size_t        name_len;
	int           rc;

	D_ASSERT(path_len > 0);

	D_ALLOC(key, key_prefix_len + path_len + 1);
	if (key == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	rc         = -DER_SUCCESS;
	name       = path;
	name_len   = 0;
	key_prefix = dcache->dd_key_root_prefix;
	parent     = NULL;
	for (;;) {
		size_t key_len;

		memcpy(key, key_prefix, key_prefix_len);
		memcpy(key + key_prefix_len, name, name_len);
		key_len      = key_prefix_len + name_len;
		key[key_len] = '\0';

		rec_tmp = dcache_get(dcache, key, key_len);
		D_DEBUG(DB_TRACE, "dcache %s: path=" DF_PATH ", key=" DF_DK "\n",
			(rec_tmp == NULL) ? "miss" : "hit", DP_PATH(path), DP_DK(key));
		if (rec_tmp == NULL) {
			char   tmp;

			D_ASSERT(name_len > 0);
			D_ASSERT(parent != NULL);

			tmp            = name[name_len];
			name[name_len] = '\0';
			rc             = dcache_add(dcache, parent, name, key, key_len, &rec_tmp);
			name[name_len] = tmp;
			if (rc != -DER_SUCCESS) {
				drec_decref(dcache, parent);
				D_GOTO(out, rc);
			}
		}
		D_ASSERT(rec_tmp != NULL);

		if (parent != NULL)
			drec_decref(dcache, parent);

		// NOTE skip '/' character
		name += name_len + 1;
		name_len = 0;
		while (name + name_len < path + path_len && name[name_len] != '/')
			++name_len;
		if (name_len == 0)
			break;

		key_prefix = &rec_tmp->dr_key_child_prefix[0];
		parent     = rec_tmp;
	}
	*rec = rec_tmp;

out:
	D_FREE(key);

	return rc;
}

dfs_obj_t *
drec2obj(dcache_rec_t *rec)
{
	return (rec == NULL) ? NULL : rec->dr_obj;
}

static void
drec_incref_act(dfs_dcache_t *dcache, dcache_rec_t *rec)
{
	d_hash_rec_addref(&dcache->dd_dir_hash, &rec->dr_entry);
}

static void
drec_decref_act(dfs_dcache_t *dcache, dcache_rec_t *rec)
{
	d_hash_rec_decref(&dcache->dd_dir_hash, &rec->dr_entry);
}

static void
drec_del_at_act(dfs_dcache_t *dcache, dcache_rec_t *rec)
{
	d_hash_rec_decref(&dcache->dd_dir_hash, &rec->dr_entry);
	if (!atomic_flag_test_and_set(&rec->dr_deleted))
		d_hash_rec_delete_at(&dcache->dd_dir_hash, &rec->dr_entry);
}

static int
drec_del_act(dfs_dcache_t *dcache, char *path, dcache_rec_t *parent)
{
	const size_t  key_prefix_len = DCACHE_KEY_PREF_SIZE - 1;
	size_t        path_len;
	char         *bname = NULL;
	size_t        bname_len;
	char         *key = NULL;
	size_t        key_len;
	d_list_t     *rlink;
	dcache_rec_t *rec;
	int           rc;

	D_ASSERT(path != NULL && path[0] == '/' && path[1] != '\0');
	D_ASSERT(parent != NULL);

	path_len = strnlen(path, PATH_MAX);
	D_ASSERT(path_len < PATH_MAX);
	bname = path + path_len - 1;
	while (bname > path && *bname != '/')
		--bname;
	++bname;
	bname_len = (path_len + path) - bname;

	key_len = key_prefix_len + bname_len;
	D_ALLOC(key, key_len + 1);
	if (key == NULL)
		D_GOTO(out, rc = -DER_NOMEM);
	memcpy(key, parent->dr_key_child_prefix, key_prefix_len);
	memcpy(key + key_prefix_len, bname, bname_len);
	key[key_len] = '\0';

	rlink = d_hash_rec_find(&dcache->dd_dir_hash, key, key_len);
	if (rlink == NULL)
		D_GOTO(out, rc = -DER_NONEXIST);

	rec = dlist2drec(rlink);
	drec_del_at(dcache, rec);
	rc = -DER_SUCCESS;

out:
	D_FREE(key);
	return rc;
}

static int
dcache_destroy_dact(dfs_dcache_t *dcache);
static int
dcache_find_insert_dact(dfs_dcache_t *dcache, char *path, size_t path_len, dcache_rec_t **rec);
static void
drec_del_at_dact(dfs_dcache_t *dcache, dcache_rec_t *rec);

static int
dcache_create_dact(dfs_t *dfs, dfs_dcache_t **dcache)
{
	dfs_dcache_t *dcache_tmp;
	int           rc;

	D_ALLOC_PTR(dcache_tmp);
	if (dcache_tmp == NULL)
		D_GOTO(error, rc = -DER_NOMEM);

	dcache_tmp->destroy_fn     = dcache_destroy_dact;
	dcache_tmp->find_insert_fn = dcache_find_insert_dact;
	dcache_tmp->drec_incref_fn = NULL;
	dcache_tmp->drec_decref_fn = drec_del_at_dact;
	dcache_tmp->drec_del_at_fn = drec_del_at_dact;
	dcache_tmp->drec_del_fn    = NULL;

	dcache_tmp->dd_dfs = dfs;

	*dcache = dcache_tmp;
	D_GOTO(out, rc = -DER_SUCCESS);

error:
	D_FREE(dcache_tmp);
out:
	return rc;
}

static int
dcache_destroy_dact(dfs_dcache_t *dcache)
{
	D_FREE(dcache);

	return -DER_SUCCESS;
}

static int
dcache_find_insert_dact(dfs_dcache_t *dcache, char *path, size_t path_len, dcache_rec_t **rec)
{
	dcache_rec_t *rec_tmp = NULL;
	dfs_obj_t    *obj     = NULL;
	mode_t        mode;
	int           rc;

	(void)path_len; /* unused */

	D_ALLOC_PTR(rec_tmp);
	if (rec_tmp == NULL)
		D_GOTO(error, rc = -DER_NOMEM);

	rc = dfs_lookup(dcache->dd_dfs, path, O_RDWR, &obj, &mode, NULL);
	if (rc != 0)
		D_GOTO(error, rc = daos_errno2der(rc));
	if (!S_ISDIR(mode))
		D_GOTO(error, rc = -DER_NOTDIR);
	rec_tmp->dr_obj = obj;

	D_DEBUG(DB_TRACE, "create record %p: path=" DF_PATH, rec_tmp, DP_PATH(path));
	*rec = rec_tmp;
	D_GOTO(out, rc = -DER_SUCCESS);

error:
	dfs_release(obj);
	D_FREE(rec_tmp);
out:
	return rc;
}

static void
drec_del_at_dact(dfs_dcache_t *dcache, dcache_rec_t *rec)
{
	int rc;

	D_DEBUG(DB_TRACE, "delete record %p", rec);

	rc = dfs_release(rec->dr_obj);
	if (rc)
		DS_ERROR(rc, "dfs_release() failed");
	D_FREE(rec);
}

int
dcache_create(dfs_t *dfs, uint32_t bits, uint32_t rec_timeout, dfs_dcache_t **dcache)
{
	D_ASSERT(dcache != NULL);
	D_ASSERT(dfs != NULL);

	if (rec_timeout == 0) {
		return dcache_create_dact(dfs, dcache);
	}

	return dcache_create_act(dfs, bits, rec_timeout, dcache);
}
int
dcache_destroy(dfs_dcache_t *dcache)
{
	D_ASSERT(dcache != NULL);
	D_ASSERT(dcache->destroy_fn != NULL);

	return dcache->destroy_fn(dcache);
}

int
dcache_find_insert(dfs_dcache_t *dcache, char *path, size_t path_len, dcache_rec_t **rec)
{
	D_ASSERT(rec != NULL);
	D_ASSERT(dcache != NULL);
	D_ASSERT(path != NULL);
	D_ASSERT(dcache->find_insert_fn != NULL);

	return dcache->find_insert_fn(dcache, path, path_len, rec);
}

void
drec_incref(dfs_dcache_t *dcache, dcache_rec_t *rec)
{
	if (rec == NULL)
		return;
	D_ASSERT(dcache != NULL);
	if (dcache->drec_incref_fn == NULL)
		return;

	dcache->drec_incref_fn(dcache, rec);
}

void
drec_decref(dfs_dcache_t *dcache, dcache_rec_t *rec)
{
	if (rec == NULL)
		return;
	D_ASSERT(dcache != NULL);
	D_ASSERT(dcache->drec_decref_fn != NULL);

	dcache->drec_decref_fn(dcache, rec);
}

void
drec_del_at(dfs_dcache_t *dcache, dcache_rec_t *rec)
{
	if (rec == NULL)
		return;
	D_ASSERT(dcache != NULL);
	D_ASSERT(dcache->drec_del_at_fn != NULL);

	dcache->drec_del_at_fn(dcache, rec);
}

int
drec_del(dfs_dcache_t *dcache, char *path, dcache_rec_t *parent)
{
	D_ASSERT(dcache != NULL);
	if (dcache->drec_del_fn == NULL)
		return -DER_SUCCESS;

	return dcache->drec_del_fn(dcache, path, parent);
}
