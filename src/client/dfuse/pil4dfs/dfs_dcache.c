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
#include <gurt/abt.h>
#include <gurt/list.h>
#include <daos_fs.h>
#include <daos/common.h>

#include "dfs_dcache.h"

#define DF_TS                "%ld.%09ld"
#define DP_TS(t)             (t).tv_sec, (t).tv_nsec

typedef int (*destroy_fn_t)(dfs_dcache_t *);
typedef int (*find_insert_fn_t)(dfs_dcache_t *, char *, size_t, dcache_rec_t **);
typedef void (*drec_incref_fn_t)(dfs_dcache_t *, dcache_rec_t *);
typedef void (*drec_decref_fn_t)(dfs_dcache_t *, dcache_rec_t *);
typedef void (*drec_del_at_fn_t)(dfs_dcache_t *, dcache_rec_t *);
typedef int (*drec_del_fn_t)(dfs_dcache_t *, char *, dcache_rec_t *);


/** Record of a DFS directory cache */
struct dcache_rec {
	/** Name of the directory */
	char               dr_name[NAME_MAX + 1];
	/** Cached DFS directory */
	dfs_obj_t         *dr_obj;
	/** Parent of this rec in the file tree */
	struct dcache_rec *dr_parent;
	/** Red Black tree holding the childs of this rec in the file tree */
	struct d_rtbt     *dr_childs;
	/** RW lock protecting the RBT */
	pthread_rwlock_t   dr_childs_lock;
	/** Reference counter used to manage memory deallocation */
	_Atomic uint32_t   dr_ref;
	/** True iff this record was deleted from the hash table*/
	atomic_flag        dr_deleted;
	/** Entry in the garbage collector list */
	d_list_t           dr_entry_gc;
	/** True iff this record is not in the garbage collector list */
	bool               dr_deleted_gc;
	/** Expiration date of the record */
	struct timespec    dr_expire_gc;
};

/** DFS directory cache */
struct dfs_dcache {
	/** Cached DAOS file system */
	dfs_t              *dd_dfs;
	/** File tree holding the cached directories */
	struct dcache_rec  *dd_file_tree;
	/** Garbage collector time-out of a dir-cache record in seconds */
	uint32_t            dd_timeout_rec;
	/** Entry head of the garbage collector list */
	d_list_t            dd_head_gc;
	/** Mutex protecting access to the garbage collector list */
	pthread_mutex_t     dd_mutex_gc;
	/** Size of the garbage collector list */
	uint64_t            dd_count_gc;
	/** Time period of garbage collection */
	uint32_t            dd_period_gc;
	/** Maximal number of dir-cache record to reclaim per garbage collector trigger */
	uint32_t            dd_reclaim_max_gc;
	/** Next Garbage collection date */
	struct timespec     dd_expire_gc;
	/** True iff one thread is running the garbage collection */
	atomic_flag         dd_running_gc;
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

static int
dcache_key_cmp(const void *key0, const void *key1)
{
	return strncmp(key0, key1, NAME_MAX + 1);
}

static void
dcache_key_free(void *key)
{
	D_FREE(key);
}

static void
dcache_rec_free(void *key)
{
	// TODO Check if needed
}

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

	D_ALLOC_PTR(rec);
	if (rec == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	memcpy(rec->dr_name, "/", sizeof("/"));
	rec->dr_obj    = obj;
	rec->dr_parent = NULL;
	atomic_init(&rec->dr_ref, 0);
	atomic_flag_clear(&rec->dr_deleted);

	rc = D_RWLOCK_INIT(&rec->dr_childs_lock, NULL);
	if (unlikely(rc) != 0)
		D_GOTO(error_rec, rc);

	rc = d_rbt_create(&rec->dr_childs, dcache_key_cmp, dcache_key_free, dcache_rec_free);
	if (unlikely(rc) != 0)
		D_GOTO(error_childs, rc);
	D_GOTO(out, rc = -DER_SUCCESS);

error_childs:
	D_RWLOCK_DESTROY(&rec->dr_childs);
error_rec:
	D_FREE(rec);
out:
	return rc;
}

static int
dcache_create_act(dfs_t *dfs, uint32_t bits, uint32_t rec_timeout, uint32_t gc_period,
		  uint32_t gc_reclaim_max, dfs_dcache_t **dcache)
{
	dfs_dcache_t *dcache_tmp;
	dfs_obj_t    *obj;
	mode_t        mode;
	int           rc;

	D_ALLOC_PTR(dcache_tmp);
	if (dcache_tmp == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	dcache_tmp->dd_dfs            = dfs;
	dcache_tmp->dd_timeout_rec    = rec_timeout;
	dcache_tmp->dd_period_gc      = gc_period;
	dcache_tmp->dd_reclaim_max_gc = gc_reclaim_max;
	dcache_tmp->dd_count_gc       = 0;
	dcache_tmp->destroy_fn        = dcache_destroy_act;
	dcache_tmp->find_insert_fn    = dcache_find_insert_act;
	dcache_tmp->drec_incref_fn    = drec_incref_act;
	dcache_tmp->drec_decref_fn    = drec_decref_act;
	dcache_tmp->drec_del_at_fn    = drec_del_at_act;
	dcache_tmp->drec_del_fn       = drec_del_act;

	atomic_flag_clear(&dcache_tmp->dd_running_gc);
	D_INIT_LIST_HEAD(&dcache_tmp->dd_head_gc);

	rc = D_MUTEX_INIT(&dcache_tmp->dd_mutex_gc, NULL);
	if (unlikely(rc != 0))
		D_GOTO(error_dcache, daos_errno2der(rc));

	rc = clock_gettime(CLOCK_MONOTONIC_COARSE, &dcache_tmp->dd_expire_gc);
	if (unlikely(rc != 0))
		D_GOTO(error_mutex, rc = d_errno2der(errno));
	dcache_tmp->dd_expire_gc.tv_sec += dcache_tmp->dd_period_gc;

	rc = dfs_lookup(dfs, "/", O_RDWR, &obj, &mode, NULL);
	if (rc != 0)
		D_GOTO(error_mutex, rc = daos_errno2der(rc));
	rc = dcache_add_root(dcache_tmp, obj);
	if (rc != 0)
		D_GOTO(error_obj, rc);

	*dcache = dcache_tmp;
	D_GOTO(out, rc = -DER_SUCCESS);

error_obj:
	dfs_release(obj);
error_mutex:
	D_MUTEX_DESTROY(&dcache_tmp->dd_mutex_gc);
error_dcache:
	D_FREE(dcache_tmp);
out:
	return rc;
}

static int
dcache_destroy_act(dfs_cache_t *dcache)
{
	// TODO
}

// TODO Reference counter management
static inline dcache_rec_t *
dcache_get(dcache_rec_t *rec, const char *name)
{
	dcache_rec_t *rec_tmp;
	int           rc;

	D_RWLOCK_RDLOCK(&rec->dr_childs_lock);
	rc = d_rbt_find(&rec_tmp, rec->dr_childs, name);
	D_RWLOCK_UNLOCK(&rec->dr_childs_lock);

	if (rc == -DER_NONEXIST)
		return NULL;
	D_ASSERT(rc == -DER_SUCCESS);

	return rec_tmp;
}

// TODO Reference counter management
static inline int
dcache_add(dfs_dcache_t *dcache, dcache_rec_t *parent, const char *name, dcache_rec_t **rec)
{
	dcache_rec_t *rec_tmp;
	d_rbt_node_t *node;
	mode_t        mode;
	dfs_obj_t    *obj;
	size_t        name_len;
	int           rc;

	rc = dfs_lookup_rel(dcache->dd_dfs, parent->dr_obj, name, O_RDWR, &obj, &mode, NULL);
	if (rc != 0)
		D_GOTO(out, rc = daos_errno2der(rc));
	if (!S_ISDIR(mode))
		D_GOTO(error_obj, rc = -DER_NOTDIR);

	D_ALLOC_PTR(rec_tmp);
	if (rec_tmp == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	name_len = strnlen(name, NAME_MAX + 1);
	if (name_len >= NAME_MAX +1)
		D_GOTO(error_rec, rc = -DER_INVAL);
	memcpy(rec_tmp->dr_name, name, name_len + 1);
	rec_tmp->dr_obj    = obj;
	rec_tmp->dr_parent = NULL;
	atomic_init(&rec_tmp->dr_ref, 0);
	atomic_flag_clear(&rec_tmp->dr_deleted);

	rc = D_RWLOCK_INIT(&rec_tmp->dr_childs_lock, NULL);
	if (unlikely(rc) != 0)
		D_GOTO(error_rec, rc);

	rc = d_rbt_create(&rec_tmp->dr_childs, dcache_key_cmp, dcache_node_free);
	if (unlikely(rc) != 0)
		D_GOTO(error_childs, rc);
	**data_new(out, rc = -DER_SUCCESS);

	D_RWLOCK_WRLOCK(&parent->dr_childs_lock);
	rc = d_rbt_find_insert(parent->dr_childs, rec_tmp->dr_key, rec_tmp, &node);
	D_RWLOCK_UNLOCK(&parent->dr_childs_lock);
	switch (rc) {
	case -DER_SUCCESS:
		D_DEBUG(DB_TRACE, "add record " DF_DK " with ref counter %u",
			DP_DK(rec_tmp->dr_key), rec_tmp->dr_ref);
		gc_add_rec(dcache, rec_tmp);
		break;
	case -DER_EXIST:
		rbt_node_free(node);
		rec_tmp = node->rn_data;
		break;
	default:
		D_GOTO(error_rbt, rc);
	}

	*rec = rec_tmp;
	D_GOTO(out, rc = -DER_SUCCESS;

error_rbt:
	d_rbt_destroy(rec_tmp->dr_childs);
error_childs:
	D_RWLOCK_DESTROY(&rec_tmp->dr_childs_lock);
error_rec:
	D_FREE(rec_tmp);
error_obj:
	dfs_release(obj);
out:
	return rc;
}

static int
dcache_find_insert_act(dfs_dcache_t *dcache, char *path, size_t path_len, dcache_rec_t **rec)
{
	char *token;
	char *subpath;
	char *saveptr = NULL;
	dcache_rec_t *rec_tmp;
	int   rc;

	D_STRNDUP(subpath, path, MAX_PATH - 1);
	if (subpath == NULL)
		D_GOTO(error, rc = -DER_NOMEM);;

	rec_tmp = dcache->dd_file_tree;
	token   = strtok_r(subpath, "/", &saveptr)
	while (token != NULL) {
		rec_tmp = dcache_get(rec_tmp, token);
		if (rec_tmp == NULL) {
			rc  = dcache_add(rec_tmp, token, &rec_tmp);
			if (rc != -DER_SUCCESS)
				D_GOTO(error);
		}

		token = strtok_r(NULL, "/", &saveptr)
	}

	*rec =rec_tmp;
	rc = -DER_SUCCESS;

error:
	D_FREE(subpath);

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
	dcache_rec_t *rec_tmp;
	dfs_obj_t    *obj;
	mode_t        mode;
	int           rc;

	(void)path_len; /* unused */

	D_ALLOC_PTR(rec_tmp);
	if (rec_tmp == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	/* NOTE Path walk will needs to be done in pil4dfs.  Indeed, dfs supports one container but
	 * all other middlewares layered on top if it will detect UNS links on directories and open
	 * a new container as necessary. To do that the path walk needs to happen at the higher
	 * level, check the xattr on lookup and open new pools/containers as required.  More details
	 * could be found at
	 * https://github.com/daos-stack/daos/blob/master/docs/user/filesystem.md#unified-namespace-uns
	 */
	rc = dfs_lookup(dcache->dd_dfs, path, O_RDWR, &obj, &mode, NULL);
	if (rc != 0)
		D_GOTO(error_rec, rc = daos_errno2der(rc));
	if (!S_ISDIR(mode))
		D_GOTO(error_obj, rc = -DER_NOTDIR);
	rec_tmp->dr_obj = obj;

	D_DEBUG(DB_TRACE, "create record %p: path=" DF_PATH, rec_tmp, DP_PATH(path));
	*rec = rec_tmp;
	D_GOTO(out, rc = -DER_SUCCESS);

error_obj:
	dfs_release(obj);
error_rec:
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
dcache_create(dfs_t *dfs, uint32_t bits, uint32_t rec_timeout, uint32_t gc_period,
	      uint32_t gc_reclaim_max, dfs_dcache_t **dcache)
{
	D_ASSERT(dcache != NULL);
	D_ASSERT(dfs != NULL);

	if (rec_timeout == 0)
		return dcache_create_dact(dfs, dcache);

	return dcache_create_act(dfs, bits, rec_timeout, gc_period, gc_reclaim_max, dcache);
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
