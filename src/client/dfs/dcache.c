/**
 * (C) Copyright 2022-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC DD_FAC(dfs)

#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>

#include <daos/debug.h>
#include <gurt/hash.h>
#include <gurt/list.h>
#include <daos/common.h>

#include "dfs_internal.h"

#define DF_TS                "%ld.%09ld"
#define DP_TS(t)             (t).tv_sec, (t).tv_nsec

#ifdef DAOS_BUILD_RELEASE

#define DF_DK      "dk[%zi]"
#define DP_DK(_dk) strnlen((_dk), DCACHE_KEY_MAX)

#else

#define DF_DK      "dk'%s'"
#define DP_DK(_dk) daos_dk2str(_dk)

static inline char *
daos_dk2str(const char *dk)
{
	int         i, j;
	const char *it;
	const char *sep = "-:";

	if (dk == NULL)
		return "<NULL>";

	it = dk;
	for (i = 0; i < 2; i++) {
		for (j = 0; j < 16; j++) {
			if (it[j] < '0' || it[j] > 'f')
				return "<invalid dentry key>";
		}
		it += 16;
		if (it[0] != sep[i])
			return "<invalid dentry key>";
		it++;
	}

	for (i = 0; i < NAME_MAX; i++) {
		if (it[i] == '\0')
			return (char *)dk;
		if (!isprint(it[i]) || it[i] == '\'')
			return "<not printable>";
	}
	return "<dentry key too long>";
}

#endif

typedef int (*destroy_fn_t)(dfs_dcache_t *);
typedef int (*find_insert_fn_t)(dfs_dcache_t *, char *, size_t, int, dfs_obj_t **, mode_t *,
				struct stat *);
typedef int (*find_insert_rel_fn_t)(dfs_dcache_t *, dfs_obj_t *, const char *, size_t, int,
				    dfs_obj_t **, mode_t *, struct stat *);
typedef void (*drec_incref_fn_t)(dfs_dcache_t *, dfs_obj_t *);
typedef void (*drec_decref_fn_t)(dfs_dcache_t *, dfs_obj_t *);
typedef void (*drec_del_at_fn_t)(dfs_dcache_t *, dfs_obj_t *);
typedef int (*drec_del_fn_t)(dfs_dcache_t *, char *, dfs_obj_t *);

/** DFS directory cache */
struct dfs_dcache {
	/** Cached DAOS file system */
	dfs_t              *dd_dfs;
	/** Hash table holding the cached directories */
	struct d_hash_table dd_dir_hash;
	/** Key prefix of the DFS root directory */
	char                dd_key_root_prefix[DCACHE_KEY_PREF_SIZE];
	/** flag to indicate whether garbage collection is disabled or not */
	bool                 dd_disable_gc;
	/** Garbage collector time-out of a dir-cache record in seconds */
	uint32_t            dd_timeout_rec;
	/** Entry head of the garbage collector list */
	d_list_t             dd_head_gc;
	/** Mutex protecting access to the garbage collector list */
	pthread_mutex_t      dd_mutex_gc;
	/** Size of the garbage collector list */
	uint64_t             dd_count_gc;
	/** Time period of garbage collection */
	uint32_t             dd_period_gc;
	/** Maximal number of dir-cache record to reclaim per garbage collector trigger */
	uint32_t             dd_reclaim_max_gc;
	/** Next Garbage collection date */
	struct timespec      dd_expire_gc;
	/** True iff one thread is running the garbage collection */
	atomic_flag          dd_running_gc;
	/** Destroy a dfs dir-cache */
	destroy_fn_t        destroy_fn;
	/** Return the dir-cache record of a given location and insert it if needed */
	find_insert_fn_t    find_insert_fn;
	/** Return the dir-cache record of a given location and insert it if needed */
	find_insert_rel_fn_t find_insert_rel_fn;
	/** Increase the reference counter of a given dir-cache record */
	drec_incref_fn_t    drec_incref_fn;
	/** Decrease the reference counter of a given dir-cache record */
	drec_decref_fn_t    drec_decref_fn;
	/** Delete a given dir-cache record */
	drec_del_at_fn_t    drec_del_at_fn;
	/** Delete the dir-cache record of a given location */
	drec_del_fn_t       drec_del_fn;
};

static inline int64_t
time_cmp(struct timespec *t0, struct timespec *t1)
{
	if (t0->tv_sec != t1->tv_sec)
		return t0->tv_sec - t1->tv_sec;
	return t0->tv_nsec - t1->tv_nsec;
}

static inline dfs_obj_t *
gc_pop_rec(dfs_dcache_t *dcache)
{
	return d_list_pop_entry(&dcache->dd_head_gc, dfs_obj_t, dc_entry_gc);
}

static inline int
gc_init_rec(dfs_dcache_t *dcache, dfs_obj_t *rec)
{
	int rc;

	if (dcache->dd_period_gc == 0)
		return -DER_SUCCESS;

	rc = clock_gettime(CLOCK_MONOTONIC_COARSE, &rec->dc_expire_gc);
	if (unlikely(rc != 0))
		return d_errno2der(errno);
	rec->dc_expire_gc.tv_sec += dcache->dd_timeout_rec;

	atomic_flag_clear(&rec->dc_deleted);
	D_INIT_LIST_HEAD(&rec->dc_entry_gc);

	return -DER_SUCCESS;
}

static inline void
gc_add_rec(dfs_dcache_t *dcache, dfs_obj_t *rec)
{
	int rc;

	if (dcache->dd_period_gc == 0)
		return;

	rc = D_MUTEX_LOCK(&dcache->dd_mutex_gc);
	D_ASSERT(rc == 0);

	if (rec->dc_deleted_gc)
		/* NOTE Eventually happen if an entry is added and then removed before the entry has
		 * been added to the GC list*/
		D_GOTO(unlock, rc);

	d_list_add_tail(&rec->dc_entry_gc, &dcache->dd_head_gc);
	++dcache->dd_count_gc;
	D_DEBUG(DB_TRACE, "add record " DF_DK " to GC: count_gc=%" PRIu64 "\n", DP_DK(rec->dc_key),
		dcache->dd_count_gc);

unlock:
	rc = D_MUTEX_UNLOCK(&dcache->dd_mutex_gc);
	D_ASSERT(rc == 0);
}

static inline void
gc_del_rec(dfs_dcache_t *dcache, dfs_obj_t *rec)
{
	int rc;

	if (dcache->dd_period_gc == 0)
		return;

	rc = D_MUTEX_LOCK(&dcache->dd_mutex_gc);
	D_ASSERT(rc == 0);

	if (rec->dc_deleted_gc)
		/* NOTE Eventually happen if an entry is reclaimed and then removed before it has
		 * been removed from the hash table */
		D_GOTO(unlock, rc);

	if (d_list_empty(&rec->dc_entry_gc)) {
		/* NOTE Eventually happen if an entry is added and then removed before the entry has
		 * been added to the GC list*/
		rec->dc_deleted_gc = true;
		D_GOTO(unlock, rc);
	}

	D_ASSERT(dcache->dd_count_gc > 0);
	d_list_del(&rec->dc_entry_gc);
	--dcache->dd_count_gc;
	rec->dc_deleted_gc = true;
	D_DEBUG(DB_TRACE, "remove deleted record " DF_DK " from GC: count_gc=%" PRIu64 "\n",
		DP_DK(rec->dc_key), dcache->dd_count_gc);

unlock:
	rc = D_MUTEX_UNLOCK(&dcache->dd_mutex_gc);
	D_ASSERT(rc == 0);
}

static inline int
gc_reclaim(dfs_dcache_t *dcache)
{
	struct timespec now;
	uint32_t        reclaim_count;
	int             rc;

	if (dcache->dd_period_gc == 0)
		D_GOTO(out, rc = -DER_SUCCESS);

	if (atomic_flag_test_and_set(&dcache->dd_running_gc))
		D_GOTO(out, rc = -DER_SUCCESS);

	rc = clock_gettime(CLOCK_MONOTONIC_COARSE, &now);
	if (unlikely(rc != 0))
		D_GOTO(out_unset, rc = d_errno2der(errno));

	if (time_cmp(&dcache->dd_expire_gc, &now) > 0)
		D_GOTO(out_unset, rc = -DER_SUCCESS);

	D_DEBUG(DB_TRACE, "start GC reclaim at " DF_TS ": GC size=%" PRIu64 "\n", DP_TS(now),
		dcache->dd_count_gc);

	reclaim_count = 0;
	while (reclaim_count < dcache->dd_reclaim_max_gc) {
		dfs_obj_t *rec;

		rc = D_MUTEX_LOCK(&dcache->dd_mutex_gc);
		D_ASSERT(rc == 0);

		if ((rec = gc_pop_rec(dcache)) == NULL) {
			rc = D_MUTEX_UNLOCK(&dcache->dd_mutex_gc);
			D_ASSERT(rc == 0);
			break;
		}

		if (time_cmp(&rec->dc_expire_gc, &now) > 0) {
			d_list_add(&rec->dc_entry_gc, &dcache->dd_head_gc);
			rc = D_MUTEX_UNLOCK(&dcache->dd_mutex_gc);
			D_ASSERT(rc == 0);
			break;
		}

		D_ASSERT(dcache->dd_count_gc > 0);
		--dcache->dd_count_gc;
		rec->dc_deleted_gc = true;

		rc = D_MUTEX_UNLOCK(&dcache->dd_mutex_gc);
		D_ASSERT(rc == 0);

		D_DEBUG(DB_TRACE, "remove expired record " DF_DK " from GC: expire=" DF_TS "\n",
			DP_DK(rec->dc_key), DP_TS(rec->dc_expire_gc));

		if (!atomic_flag_test_and_set(&rec->dc_deleted))
			d_hash_rec_delete_at(&dcache->dd_dir_hash, &rec->dc_entry);

		++reclaim_count;
	}

	if (reclaim_count >= dcache->dd_reclaim_max_gc) {
		D_DEBUG(DB_TRACE, "yield GC reclaim: GC size=%" PRIu64 "\n", dcache->dd_count_gc);
		D_GOTO(out_unset, rc = -DER_SUCCESS);
	}

	rc = clock_gettime(CLOCK_MONOTONIC_COARSE, &now);
	if (unlikely(rc != 0))
		D_GOTO(out_unset, rc = d_errno2der(errno));
	dcache->dd_expire_gc.tv_sec  = now.tv_sec + dcache->dd_period_gc;
	dcache->dd_expire_gc.tv_nsec = now.tv_nsec;

	D_DEBUG(DB_TRACE, "stop GC reclaim at " DF_TS ": GC size=%" PRIu64 "\n", DP_TS(now),
		dcache->dd_count_gc);

out_unset:
	atomic_flag_clear(&dcache->dd_running_gc);
out:
	return rc;
}

static inline dfs_obj_t *
dlist2drec(d_list_t *rlink)
{
	return d_list_entry(rlink, dfs_obj_t, dc_entry);
}

static bool
dcache_key_cmp(struct d_hash_table *htable, d_list_t *rlink, const void *key, unsigned int key_len)
{
	dfs_obj_t *rec;

	rec = dlist2drec(rlink);
	if (rec->dc_key_len != key_len)
		return false;
	D_ASSERT(rec->dc_key[key_len] == '\0');
	D_ASSERT(((char *)key)[key_len] == '\0');

	return strncmp(rec->dc_key, (const char *)key, key_len) == 0;
}

static void
dcache_rec_free(struct d_hash_table *htable, d_list_t *rlink)
{
	int           rc;
	dfs_obj_t    *rec;

	rec = dlist2drec(rlink);
	D_DEBUG(DB_TRACE, "delete record " DF_DK " (ref=%u)", DP_DK(rec->dc_key),
		atomic_load(&rec->dc_ref));
	rc = release_int(rec);
	if (rc)
		DS_ERROR(rc, "release_int() failed");
}

static void
dcache_rec_addref(struct d_hash_table *htable, d_list_t *rlink)
{
	dfs_obj_t    *rec;
	uint32_t      oldref;

	rec    = dlist2drec(rlink);
	oldref = atomic_fetch_add(&rec->dc_ref, 1);
	D_DEBUG(DB_TRACE, "increment ref counter of record " DF_DK " from %u to %u",
		DP_DK(rec->dc_key), oldref, oldref + 1);
}

static bool
dcache_rec_decref(struct d_hash_table *htable, d_list_t *rlink)
{
	dfs_obj_t    *rec;
	uint32_t      oldref;

	rec    = dlist2drec(rlink);
	oldref = atomic_fetch_sub(&rec->dc_ref, 1);
	D_DEBUG(DB_TRACE, "decrement ref counter of record " DF_DK " from %u to %u",
		DP_DK(rec->dc_key), oldref, oldref - 1);
	D_ASSERT(oldref >= 1);
	return oldref == 1;
}

static uint32_t
dcache_rec_hash(struct d_hash_table *htable, d_list_t *rlink)
{
	dfs_obj_t *rec;

	rec = dlist2drec(rlink);
	return d_hash_string_u32(rec->dc_key, rec->dc_key_len);
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
dcache_find_insert_act(dfs_dcache_t *dcache, char *path, size_t path_len, int flags,
		       dfs_obj_t **rec, mode_t *mode, struct stat *stbuf);
static int
dcache_find_insert_rel_act(dfs_dcache_t *dcache, dfs_obj_t *parent, const char *name, size_t len,
			   int flags, dfs_obj_t **rec, mode_t *mode, struct stat *stbuf);
static void
drec_incref_act(dfs_dcache_t *dcache, dfs_obj_t *rec);
static void
drec_decref_act(dfs_dcache_t *dcache, dfs_obj_t *rec);
static void
drec_del_at_act(dfs_dcache_t *dcache, dfs_obj_t *rec);
static int
drec_del_act(dfs_dcache_t *dcache, char *path, dfs_obj_t *parent);

static inline int
dcache_add_root(dfs_dcache_t *dcache)
{
	dfs_obj_t *rec;
	int        rc;

	rc = dup_int(dcache->dd_dfs, &dcache->dd_dfs->root, O_RDWR, &rec, DCACHE_KEY_PREF_SIZE);
	if (rc)
		return rc;
	rec->dc = dcache;
	atomic_init(&rec->dc_ref, 0);
	atomic_flag_clear(&rec->dc_deleted);
	memcpy(&rec->dc_key_child_prefix[0], &dcache->dd_key_root_prefix[0], DCACHE_KEY_PREF_SIZE);
	memcpy(&rec->dc_key[0], &dcache->dd_key_root_prefix[0], DCACHE_KEY_PREF_SIZE);
	rec->dc_key_len = DCACHE_KEY_PREF_SIZE - 1;
	rc = d_hash_rec_insert(&dcache->dd_dir_hash, rec->dc_key, rec->dc_key_len, &rec->dc_entry,
			       true);
	if (rc)
		release_int(rec);
	return daos_der2errno(rc);
}

static int
dcache_create_act(dfs_t *dfs, uint32_t bits, uint32_t rec_timeout, uint32_t gc_period,
		  uint32_t gc_reclaim_max)
{
	dfs_dcache_t *dcache_tmp;
	int           rc;

	D_ALLOC_PTR(dcache_tmp);
	if (dcache_tmp == NULL)
		return ENOMEM;

	dcache_tmp->dd_dfs             = dfs;
	dcache_tmp->destroy_fn        = dcache_destroy_act;
	dcache_tmp->find_insert_fn    = dcache_find_insert_act;
	dcache_tmp->find_insert_rel_fn = dcache_find_insert_rel_act;
	dcache_tmp->drec_incref_fn    = drec_incref_act;
	dcache_tmp->drec_decref_fn    = drec_decref_act;
	dcache_tmp->drec_del_at_fn    = drec_del_at_act;
	dcache_tmp->drec_del_fn       = drec_del_act;

	if (rec_timeout == 0) {
		dcache_tmp->dd_disable_gc = true;
	} else {
		dcache_tmp->dd_timeout_rec    = rec_timeout;
		dcache_tmp->dd_period_gc      = gc_period;
		dcache_tmp->dd_reclaim_max_gc = gc_reclaim_max;
		dcache_tmp->dd_count_gc       = 0;
		dcache_tmp->dd_disable_gc     = false;

		rc = D_MUTEX_INIT(&dcache_tmp->dd_mutex_gc, NULL);
		if (rc != 0)
			D_GOTO(error_alloc, daos_errno2der(rc));
		atomic_flag_clear(&dcache_tmp->dd_running_gc);
		D_INIT_LIST_HEAD(&dcache_tmp->dd_head_gc);

		rc = clock_gettime(CLOCK_MONOTONIC_COARSE, &dcache_tmp->dd_expire_gc);
		if (unlikely(rc != 0))
			D_GOTO(error_mutex, rc = d_errno2der(errno));
		dcache_tmp->dd_expire_gc.tv_sec += dcache_tmp->dd_period_gc;
	}

	rc = snprintf(&dcache_tmp->dd_key_root_prefix[0], DCACHE_KEY_PREF_SIZE,
		      "%016" PRIx64 "-%016" PRIx64 ":", dfs->root.oid.hi, dfs->root.oid.lo);
	D_ASSERT(rc == DCACHE_KEY_PREF_SIZE - 1);

	rc = d_hash_table_create_inplace(D_HASH_FT_MUTEX | D_HASH_FT_LRU, bits, NULL,
					 &dcache_hash_ops, &dcache_tmp->dd_dir_hash);
	if (rc != 0)
		D_GOTO(error_mutex, rc = daos_der2errno(rc));

	rc = dcache_add_root(dcache_tmp);
	if (rc != 0)
		D_GOTO(error_htable, rc);

	dfs->dcache = dcache_tmp;
	return 0;

error_htable:
	d_hash_table_destroy_inplace(&dcache_tmp->dd_dir_hash, true);
error_mutex:
	if (dcache_tmp->dd_disable_gc == false)
		D_MUTEX_DESTROY(&dcache_tmp->dd_mutex_gc);
error_alloc:
	D_FREE(dcache_tmp);
	return rc;
}

static int
dcache_destroy_act(dfs_dcache_t *dcache)
{
	d_list_t *rlink;
	int       rc;

	while ((rlink = d_hash_rec_first(&dcache->dd_dir_hash)) != NULL) {
		dfs_obj_t *rec;
		bool       deleted;

		rec = dlist2drec(rlink);
		/* '/' is never in the GC list */
		if (!dcache->dd_disable_gc && rec->dc_key_len != DCACHE_KEY_PREF_SIZE - 1)
			gc_del_rec(dcache, rec);
		D_ASSERT(atomic_load(&rec->dc_ref) == 1);
		deleted = d_hash_rec_delete_at(&dcache->dd_dir_hash, rlink);
		D_ASSERT(deleted);
	}
	D_ASSERT(dcache->dd_count_gc == 0);

	rc = d_hash_table_destroy_inplace(&dcache->dd_dir_hash, false);
	if (rc != 0) {
		DL_ERROR(rc, "d_hash_table_destroy_inplace() failed");
		return daos_der2errno(rc);
	}

	if (!dcache->dd_disable_gc) {
		rc = D_MUTEX_DESTROY(&dcache->dd_mutex_gc);
		if (rc != 0) {
			DL_ERROR(rc, "D_MUTEX_DESTROY() failed");
			return daos_der2errno(rc);
		}
	}

	D_FREE(dcache);
	return 0;
}

static inline dfs_obj_t *
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
dcache_add(dfs_dcache_t *dcache, dfs_obj_t *parent, const char *name, size_t len, const char *key,
	   size_t key_len, dfs_obj_t **rec, mode_t *mode, struct stat *stbuf)
{
	dfs_obj_t *obj = NULL;
	d_list_t  *rlink;
	int        rc;

	D_DEBUG(DB_TRACE, "DCACHE add: parent %s name %s key %s\n", parent->name, name, key);
	rc = lookup_rel_int(dcache->dd_dfs, parent, name, len, O_RDWR | O_NOFOLLOW, &obj, mode,
			    stbuf, 0, NULL, NULL, NULL, key_len + 1);
	if (rc)
		return rc;

	if (stbuf) {
		memcpy(&obj->dc_stbuf, stbuf, sizeof(struct stat));
		obj->dc_stated = true;
	}

	obj->dc = dcache;
	atomic_init(&obj->dc_ref, 1);
	atomic_flag_clear(&obj->dc_deleted);
	rc = snprintf(&obj->dc_key_child_prefix[0], DCACHE_KEY_PREF_SIZE,
		      "%016" PRIx64 "-%016" PRIx64 ":", obj->oid.hi, obj->oid.lo);
	D_ASSERT(rc == DCACHE_KEY_PREF_SIZE - 1);
	obj->dc_key_len = key_len;
	memcpy(obj->dc_key, key, key_len + 1);

	if (!dcache->dd_disable_gc) {
		rc = gc_init_rec(dcache, obj);
		if (rc != 0) {
			dfs_release(obj);
			return rc;
		}
	}

	rlink = d_hash_rec_find_insert(&dcache->dd_dir_hash, obj->dc_key, key_len, &obj->dc_entry);
	if (rlink == &obj->dc_entry) {
		D_DEBUG(DB_TRACE, "add record " DF_DK " with ref counter %u", DP_DK(obj->dc_key),
			obj->dc_ref);
		if (!dcache->dd_disable_gc)
			gc_add_rec(dcache, obj);
	} else {
		dcache_rec_free(&dcache->dd_dir_hash, &obj->dc_entry);
		obj = dlist2drec(rlink);
	}

	*rec = obj;
	return 0;
}

static int
dcache_find_insert_rel_act(dfs_dcache_t *dcache, dfs_obj_t *parent, const char *name, size_t len,
			   int flags, dfs_obj_t **_rec, mode_t *mode, struct stat *stbuf)
{
	const size_t key_prefix_len = DCACHE_KEY_PREF_SIZE - 1;
	dfs_obj_t   *rec;
	char        *key;
	char        *key_prefix;
	size_t       key_len;
	int          rc;

	D_ALLOC(key, key_prefix_len + len + 1);
	if (key == NULL)
		return -DER_NOMEM;

	key_prefix = dcache->dd_key_root_prefix;
	memcpy(key, key_prefix, key_prefix_len);
	memcpy(key + key_prefix_len, name, len);
	key_len      = key_prefix_len + len;
	key[key_len] = '\0';

	rec = dcache_get(dcache, key, key_len);
	D_DEBUG(DB_TRACE, "dcache %s: name=" DF_PATH ", key=" DF_DK "\n",
		(rec == NULL) ? "miss" : "hit", DP_PATH(name), DP_DK(key));
	if (rec == NULL) {
		rc = dcache_add(dcache, parent, name, len, key, key_len, &rec, mode, stbuf);
		if (rc)
			D_GOTO(out, rc);
	} else {
		/** handle following symlinks outside of the dcache */
		if (S_ISLNK(rec->mode) && !(flags & O_NOFOLLOW)) {
			dfs_obj_t *sym;

			rc = lookup_rel_path(dcache->dd_dfs, parent, rec->value, flags, &sym, mode,
					     stbuf, 0);
			drec_decref(dcache, rec);
			if (rc)
				D_GOTO(out, rc);
			rec = sym;
			D_GOTO(done, rc);
		}
		if (stbuf) {
			if (rec->dc_stated) {
				memcpy(stbuf, &rec->dc_stbuf, sizeof(struct stat));
			} else {
				rc = entry_stat(dcache->dd_dfs, DAOS_TX_NONE, parent->oh, name, len,
						NULL, true, stbuf, NULL);
				if (rc != 0)
					D_GOTO(out, rc);
				memcpy(&rec->dc_stbuf, stbuf, sizeof(struct stat));
				rec->dc_stated = true;
			}
		}
		if (mode)
			*mode = rec->mode;
		rc = 0;
	}

done:
	D_ASSERT(rec != NULL);
	*_rec = rec;
	if (!dcache->dd_disable_gc) {
		rc = gc_reclaim(dcache);
		if (rc != 0) {
			DS_WARN(daos_der2errno(rc), "Garbage collection of dcache failed");
			rc = 0;
		}
	}
out:
	D_FREE(key);
	return rc;
}

static int
dcache_find_insert_act(dfs_dcache_t *dcache, char *path, size_t path_len, int flags,
		       dfs_obj_t **_rec, mode_t *mode, struct stat *stbuf)
{
	const size_t  key_prefix_len = DCACHE_KEY_PREF_SIZE - 1;
	dfs_obj_t    *rec;
	dfs_obj_t    *parent;
	char         *key;
	char         *key_prefix;
	char         *name;
	size_t        name_len;
	bool          skip_stat = false;
	int           rc;

	D_ASSERT(path_len > 0);

	D_ALLOC(key, key_prefix_len + path_len + 1);
	if (key == NULL)
		return -DER_NOMEM;

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

		rec = dcache_get(dcache, key, key_len);
		D_DEBUG(DB_TRACE, "dcache %s: path=" DF_PATH ", key=" DF_DK "\n",
			(rec == NULL) ? "miss" : "hit", DP_PATH(path), DP_DK(key));
		if (rec == NULL) {
			char tmp;

			D_ASSERT(name_len > 0);
			D_ASSERT(parent != NULL);

			tmp            = name[name_len];
			name[name_len] = '\0';
			/** don't pass stbuf and mode if this is not the last entry */
			if (name + name_len == path + path_len) {
				rc = dcache_add(dcache, parent, name, name_len, key, key_len, &rec,
						mode, stbuf);
				/** stbuf and mode are filled already if valid, so skip later */
				skip_stat = true;
			} else {
				rc = dcache_add(dcache, parent, name, name_len, key, key_len, &rec,
						NULL, NULL);
			}
			name[name_len] = tmp;
			if (rc) {
				drec_decref(dcache, parent);
				D_GOTO(out, rc);
			}
		}
		D_ASSERT(rec != NULL);

		if (parent != NULL)
			drec_decref(dcache, parent);

		// NOTE skip '/' character
		name += name_len + 1;
		name_len = 0;
		while (name + name_len < path + path_len && name[name_len] != '/')
			++name_len;
		if (name_len == 0) {
			/** handle following symlinks outside of the dcache */
			if (S_ISLNK(rec->mode) && !(flags & O_NOFOLLOW)) {
				dfs_obj_t *sym;

				rc = lookup_rel_path(dcache->dd_dfs, parent, rec->value, flags,
						     &sym, mode, stbuf, 0);
				drec_decref(dcache, rec);
				if (rc)
					D_GOTO(out, rc);
				rec = sym;
				D_GOTO(done, rc);
			}
			break;
		}

		key_prefix = &rec->dc_key_child_prefix[0];
		parent     = rec;
	}

	if (stbuf && !skip_stat) {
		if (rec->dc_stated) {
			memcpy(stbuf, &rec->dc_stbuf, sizeof(struct stat));
		} else {
			rc = entry_stat(dcache->dd_dfs, DAOS_TX_NONE, parent->oh, rec->name,
					strlen(rec->name), rec, true, stbuf, NULL);
			if (rc != 0)
				D_GOTO(out, rc);
			memcpy(&rec->dc_stbuf, stbuf, sizeof(struct stat));
			rec->dc_stated = true;
		}
	}
	if (mode && !skip_stat)
		*mode = rec->mode;

done:
	*_rec = rec;
	if (!dcache->dd_disable_gc) {
		rc = gc_reclaim(dcache);
		if (rc != 0) {
			DS_WARN(daos_der2errno(rc), "Garbage collection of dcache failed");
			rc = 0;
		}
	}
out:
	D_FREE(key);
	return rc;
}

static void
drec_incref_act(dfs_dcache_t *dcache, dfs_obj_t *rec)
{
	d_hash_rec_addref(&dcache->dd_dir_hash, &rec->dc_entry);
}

static void
drec_decref_act(dfs_dcache_t *dcache, dfs_obj_t *rec)
{
	d_hash_rec_decref(&dcache->dd_dir_hash, &rec->dc_entry);
}

static void
drec_del_at_act(dfs_dcache_t *dcache, dfs_obj_t *rec)
{
	if (!dcache->dd_disable_gc)
		gc_del_rec(dcache, rec);
	d_hash_rec_decref(&dcache->dd_dir_hash, &rec->dc_entry);
	if (!atomic_flag_test_and_set(&rec->dc_deleted))
		d_hash_rec_delete_at(&dcache->dd_dir_hash, &rec->dc_entry);
}

static int
drec_del_act(dfs_dcache_t *dcache, char *path, dfs_obj_t *parent)
{
	const size_t  key_prefix_len = DCACHE_KEY_PREF_SIZE - 1;
	size_t        path_len;
	char         *bname = NULL;
	size_t        bname_len;
	char         *key = NULL;
	size_t        key_len;
	d_list_t     *rlink;
	dfs_obj_t    *rec;
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
	memcpy(key, parent->dc_key_child_prefix, key_prefix_len);
	memcpy(key + key_prefix_len, bname, bname_len);
	key[key_len] = '\0';

	rlink = d_hash_rec_find(&dcache->dd_dir_hash, key, key_len);
	if (rlink == NULL)
		D_GOTO(out, rc = ENOENT);

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
dcache_find_insert_dact(dfs_dcache_t *dcache, char *path, size_t path_len, int flags,
			dfs_obj_t **rec, mode_t *mode, struct stat *stbuf);
static int
dcache_find_insert_rel_dact(dfs_dcache_t *dcache, dfs_obj_t *parent, const char *name, size_t len,
			    int flags, dfs_obj_t **rec, mode_t *mode, struct stat *stbuf);
static void
drec_del_at_dact(dfs_dcache_t *dcache, dfs_obj_t *rec);

static int
dcache_create_dact(dfs_t *dfs)
{
	dfs_dcache_t *dcache_tmp;

	D_ALLOC_PTR(dcache_tmp);
	if (dcache_tmp == NULL)
		return ENOMEM;

	dcache_tmp->destroy_fn     = dcache_destroy_dact;
	dcache_tmp->find_insert_fn = dcache_find_insert_dact;
	dcache_tmp->find_insert_rel_fn = dcache_find_insert_rel_dact;
	dcache_tmp->drec_incref_fn = NULL;
	dcache_tmp->drec_decref_fn = drec_del_at_dact;
	dcache_tmp->drec_del_at_fn = drec_del_at_dact;
	dcache_tmp->drec_del_fn    = NULL;
	dcache_tmp->dd_dfs         = dfs;
	dfs->dcache = dcache_tmp;
	return 0;
}

static int
dcache_destroy_dact(dfs_dcache_t *dcache)
{
	D_FREE(dcache);
	return 0;
}

static int
dcache_find_insert_rel_dact(dfs_dcache_t *dcache, dfs_obj_t *parent, const char *name, size_t len,
			    int flags, dfs_obj_t **rec, mode_t *mode, struct stat *stbuf)
{
	return lookup_rel_int(dcache->dd_dfs, parent, name, len, O_RDWR, rec, mode, stbuf, 0, NULL,
			      NULL, NULL, 0);
}

static int
dcache_find_insert_dact(dfs_dcache_t *dcache, char *path, size_t path_len, int flags,
			dfs_obj_t **rec, mode_t *mode, struct stat *stbuf)
{
	return lookup_rel_path(dcache->dd_dfs, &dcache->dd_dfs->root, path, O_RDWR, rec, mode,
			       stbuf, 0);
}

static void
drec_del_at_dact(dfs_dcache_t *dcache, dfs_obj_t *rec)
{
	int rc;

	D_DEBUG(DB_TRACE, "delete record %p", rec);

	rc = release_int(rec);
	if (rc)
		DS_ERROR(rc, "release_int() failed");
}

int
dcache_create(dfs_t *dfs, uint32_t bits, uint32_t rec_timeout, uint32_t gc_period,
	      uint32_t gc_reclaim_max)
{
	D_ASSERT(dfs);
	if (bits == 0)
		return dcache_create_dact(dfs);

	return dcache_create_act(dfs, bits, rec_timeout, gc_period, gc_reclaim_max);
}

int
dcache_destroy(dfs_t *dfs)
{
	D_ASSERT(dfs->dcache != NULL);
	D_ASSERT(dfs->dcache->destroy_fn != NULL);

	return dfs->dcache->destroy_fn(dfs->dcache);
}

int
dcache_find_insert(dfs_t *dfs, char *path, size_t path_len, int flags, dfs_obj_t **rec,
		   mode_t *mode, struct stat *stbuf)
{
	D_ASSERT(rec != NULL);
	D_ASSERT(dfs->dcache != NULL);
	D_ASSERT(path != NULL);
	D_ASSERT(dfs->dcache->find_insert_fn != NULL);

	return dfs->dcache->find_insert_fn(dfs->dcache, path, path_len, flags, rec, mode, stbuf);
}

int
dcache_find_insert_rel(dfs_t *dfs, dfs_obj_t *parent, const char *name, size_t len, int flags,
		       dfs_obj_t **rec, mode_t *mode, struct stat *stbuf)
{
	D_ASSERT(dfs->dcache != NULL);
	D_ASSERT(name != NULL);
	D_ASSERT(dfs->dcache->find_insert_rel_fn != NULL);

	return dfs->dcache->find_insert_rel_fn(dfs->dcache, parent, name, len, flags, rec, mode,
					       stbuf);
}

void
drec_incref(dfs_dcache_t *dcache, dfs_obj_t *rec)
{
	if (rec == NULL)
		return;
	D_ASSERT(dcache != NULL);
	if (dcache->drec_incref_fn == NULL)
		return;

	dcache->drec_incref_fn(dcache, rec);
}

void
drec_decref(dfs_dcache_t *dcache, dfs_obj_t *rec)
{
	if (rec == NULL)
		return;
	D_ASSERT(dcache != NULL);
	D_ASSERT(dcache->drec_decref_fn != NULL);

	dcache->drec_decref_fn(dcache, rec);
}

void
drec_del_at(dfs_dcache_t *dcache, dfs_obj_t *rec)
{
	if (rec == NULL)
		return;
	D_ASSERT(dcache != NULL);
	D_ASSERT(dcache->drec_del_at_fn != NULL);

	dcache->drec_del_at_fn(dcache, rec);
}

int
drec_del(dfs_dcache_t *dcache, char *path, dfs_obj_t *parent)
{
	D_ASSERT(dcache != NULL);
	if (dcache->drec_del_fn == NULL)
		return -DER_SUCCESS;

	return dcache->drec_del_fn(dcache, path, parent);
}
