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

#define DF_TS                "%ld.%09ld"
#define DP_TS(t)             (t).tv_sec, (t).tv_nsec

/** Size of the hash key prefix */
#define DCACHE_KEY_PREF_SIZE 35
#define DCACHE_KEY_MAX       (DCACHE_KEY_PREF_SIZE - 1 + PATH_MAX)

#ifdef DAOS_BUILD_RELEASE

#define DF_DK          "dk[%zi]"
#define DP_DK(_dk)     strnlen((_dk), DCACHE_KEY_MAX)

#else

#define DF_DK          "dk'%s'"
#define DP_DK(_dk)     daos_dk2str(_dk)

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
	_Atomic bool        dd_running_gc;
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

/** Record of a DFS directory cache */
struct dcache_rec {
	/** Entry in the hash table of the DFS cache */
	d_list_t         dr_entry;
	/** Cached DFS directory */
	dfs_obj_t       *dr_obj;
	/** Reference counter used to manage memory deallocation */
	_Atomic uint32_t dr_ref;
	/** True iff this record was deleted from the hash table*/
	_Atomic bool     dr_deleted;
	/** Entry in the garbage collector list */
	d_list_t         dr_entry_gc;
	/** True iff this record is not in the garbage collector list */
	bool             dr_deleted_gc;
	/** Expiration date of the record */
	struct timespec  dr_expire_gc;
	/** Key prefix used by its child directory */
	char             dr_key_child_prefix[DCACHE_KEY_PREF_SIZE];
	/** Length of the hash key used to compute the hash index */
	size_t           dr_key_len;
	/** the hash key used to compute the hash index */
	char             dr_key[];
};

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

static inline int64_t
time_cmp(struct timespec *t0, struct timespec *t1)
{
	if (t0->tv_sec != t1->tv_sec)
		return t0->tv_sec - t1->tv_sec;
	return t0->tv_nsec - t1->tv_nsec;
}

static inline dcache_rec_t *
gc_pop_rec(dfs_dcache_t *dcache)
{
	return d_list_pop_entry(&dcache->dd_head_gc, dcache_rec_t, dr_entry_gc);
}

static inline int
gc_init_rec(dfs_dcache_t *dcache, dcache_rec_t *rec)
{
	int rc;

	if (dcache->dd_period_gc == 0)
		return -DER_SUCCESS;

	rc = clock_gettime(CLOCK_MONOTONIC_COARSE, &rec->dr_expire_gc);
	if (unlikely(rc != 0))
		return d_errno2der(errno);
	rec->dr_expire_gc.tv_sec += dcache->dd_timeout_rec;

	rec->dr_deleted_gc = false;
	D_INIT_LIST_HEAD(&rec->dr_entry_gc);

	return -DER_SUCCESS;
}

static inline void
gc_add_rec(dfs_dcache_t *dcache, dcache_rec_t *rec)
{
	int rc;

	if (dcache->dd_period_gc == 0)
		return;

	rc = D_MUTEX_LOCK(&dcache->dd_mutex_gc);
	D_ASSERT(rc == 0);

	if (rec->dr_deleted_gc)
		/* NOTE Eventually happen if an entry is added and then removed before the entry has
		 * been added to the GC list*/
		D_GOTO(unlock, rc);

	d_list_add_tail(&rec->dr_entry_gc, &dcache->dd_head_gc);
	++dcache->dd_count_gc;
	D_DEBUG(DB_TRACE, "add record " DF_DK " to GC: count_gc=%" PRIu64 "\n", DP_DK(rec->dr_key),
		dcache->dd_count_gc);

unlock:
	rc = D_MUTEX_UNLOCK(&dcache->dd_mutex_gc);
	D_ASSERT(rc == 0);
}

static inline void
gc_del_rec(dfs_dcache_t *dcache, dcache_rec_t *rec)
{
	int rc;

	if (dcache->dd_period_gc == 0)
		return;

	rc = D_MUTEX_LOCK(&dcache->dd_mutex_gc);
	D_ASSERT(rc == 0);

	if (rec->dr_deleted_gc)
		/* NOTE Eventually happen if an entry is reclaimed and then removed before it has
		 * been removed from the hash table */
		D_GOTO(unlock, rc);

	if (d_list_empty(&rec->dr_entry_gc)) {
		/* NOTE Eventually happen if an entry is added and then removed before the entry has
		 * been added to the GC list*/
		rec->dr_deleted_gc = true;
		D_GOTO(unlock, rc);
	}

	D_ASSERT(dcache->dd_count_gc > 0);
	d_list_del(&rec->dr_entry_gc);
	--dcache->dd_count_gc;
	rec->dr_deleted_gc = true;
	D_DEBUG(DB_TRACE, "remove deleted record " DF_DK " from GC: count_gc=%" PRIu64 "\n",
		DP_DK(rec->dr_key), dcache->dd_count_gc);

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
		dcache_rec_t *rec;

		rc = D_MUTEX_LOCK(&dcache->dd_mutex_gc);
		D_ASSERT(rc == 0);

		if ((rec = gc_pop_rec(dcache)) == NULL) {
			rc = D_MUTEX_UNLOCK(&dcache->dd_mutex_gc);
			D_ASSERT(rc == 0);
			break;
		}

		if (time_cmp(&rec->dr_expire_gc, &now) > 0) {
			d_list_add(&rec->dr_entry_gc, &dcache->dd_head_gc);
			rc = D_MUTEX_UNLOCK(&dcache->dd_mutex_gc);
			D_ASSERT(rc == 0);
			break;
		}

		D_ASSERT(dcache->dd_count_gc > 0);
		--dcache->dd_count_gc;
		rec->dr_deleted_gc = true;

		rc = D_MUTEX_UNLOCK(&dcache->dd_mutex_gc);
		D_ASSERT(rc == 0);

		D_DEBUG(DB_TRACE, "remove expired record " DF_DK " from GC: expire=" DF_TS "\n",
			DP_DK(rec->dr_key), DP_TS(rec->dr_expire_gc));

		if (!atomic_flag_test_and_set(&rec->dr_deleted))
			d_hash_rec_delete_at(&dcache->dd_dir_hash, &rec->dr_entry);

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
	atomic_store_relaxed(&dcache->dd_running_gc, false);
out:
	return rc;
}

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
dcache_create_act(dfs_t *dfs, uint32_t bits, uint32_t rec_timeout, uint32_t gc_period,
		  uint32_t gc_reclaim_max, dfs_dcache_t **dcache)
{
	dfs_dcache_t *dcache_tmp;
	dfs_obj_t    *obj;
	daos_obj_id_t obj_id;
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

	rc = dfs_lookup(dfs, "/", O_RDWR, &obj, &mode, NULL);
	if (rc != 0)
		D_GOTO(error_dfs_obj, rc = daos_errno2der(rc));
	rc = dfs_obj2id(obj, &obj_id);
	D_ASSERT(rc == 0);
	rc = snprintf(&dcache_tmp->dd_key_root_prefix[0], DCACHE_KEY_PREF_SIZE,
		      "%016" PRIx64 "-%016" PRIx64 ":", obj_id.hi, obj_id.lo);
	D_ASSERT(rc == DCACHE_KEY_PREF_SIZE - 1);

	rc = D_MUTEX_INIT(&dcache_tmp->dd_mutex_gc, NULL);
	if (rc != 0)
		D_GOTO(error_mutex, daos_errno2der(rc));
	atomic_init(&dcache_tmp->dd_running_gc, false);
	D_INIT_LIST_HEAD(&dcache_tmp->dd_head_gc);

	rc = d_hash_table_create_inplace(D_HASH_FT_MUTEX | D_HASH_FT_LRU, bits, NULL,
					 &dcache_hash_ops, &dcache_tmp->dd_dir_hash);
	if (rc != 0)
		D_GOTO(error_htable, rc);

	rc = clock_gettime(CLOCK_MONOTONIC_COARSE, &dcache_tmp->dd_expire_gc);
	if (unlikely(rc != 0))
		D_GOTO(error_htable, rc = d_errno2der(errno));
	dcache_tmp->dd_expire_gc.tv_sec += dcache_tmp->dd_period_gc;

	rc = dcache_add_root(dcache_tmp, obj);
	if (rc != 0)
		D_GOTO(error_add_root, rc);

	*dcache = dcache_tmp;
	D_GOTO(out, rc = -DER_SUCCESS);

error_add_root:
	d_hash_table_destroy_inplace(&dcache_tmp->dd_dir_hash, true);
error_htable:
	D_MUTEX_DESTROY(&dcache_tmp->dd_mutex_gc);
error_mutex:
	dfs_release(obj);
error_dfs_obj:
	D_FREE(dcache_tmp);
out:
	return rc;
}

static int
dcache_destroy_act(dfs_dcache_t *dcache)
{
	d_list_t *rlink;
	int       rc;

	while ((rlink = d_hash_rec_first(&dcache->dd_dir_hash)) != NULL) {
		dcache_rec_t *rec;
		bool          deleted;

		rec = dlist2drec(rlink);
		/* '/' is never in the GC list */
		if (rec->dr_key_len != DCACHE_KEY_PREF_SIZE - 1)
			gc_del_rec(dcache, rec);
		D_ASSERT(atomic_load(&rec->dr_ref) == 1);
		deleted = d_hash_rec_delete_at(&dcache->dd_dir_hash, rlink);
		D_ASSERT(deleted);
	}
	D_ASSERT(dcache->dd_count_gc == 0);

	rc = d_hash_table_destroy_inplace(&dcache->dd_dir_hash, false);
	if (rc != 0) {
		DL_ERROR(rc, "d_hash_table_destroy_inplace() failed");
		D_GOTO(out, rc);
	}

	rc = D_MUTEX_DESTROY(&dcache->dd_mutex_gc);
	if (rc != 0) {
		DL_ERROR(rc, "D_MUTEX_DESTROY() failed");
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

	rc = gc_init_rec(dcache, rec_tmp);
	if (rc != 0)
		D_GOTO(error, rc);

	rlink = d_hash_rec_find_insert(&dcache->dd_dir_hash, rec_tmp->dr_key, key_len,
				       &rec_tmp->dr_entry);
	if (rlink == &rec_tmp->dr_entry) {
		D_DEBUG(DB_TRACE, "add record " DF_DK " with ref counter %u",
			DP_DK(rec_tmp->dr_key), rec_tmp->dr_ref);
		gc_add_rec(dcache, rec_tmp);
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
	int           rc_gc;
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
			char tmp;

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

	rc_gc = gc_reclaim(dcache);
	if (rc_gc != 0)
		DS_WARN(rc_gc, "Garbage collection of dir cache failed");

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
	gc_del_rec(dcache, rec);

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

	/* NOTE Path walk will needs to be done in pil4dfs.  Indeed, dfs supports one container but
	 * all other middlewares layered on top if it will detect UNS links on directories and open
	 * a new container as necessary. To do that the path walk needs to happen at the higher
	 * level, check the xattr on lookup and open new pools/containers as required.  More details
	 * could be found at
	 * https://github.com/daos-stack/daos/blob/master/docs/user/filesystem.md#unified-namespace-uns
	 */
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
