/**
 * (C) Copyright 2022-2024 Intel Corporation.
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC DD_FAC(dfs)

#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>

#include <daos/pool.h>
#include <daos/container.h>
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

/** DFS dentry cache */
struct dfs_dcache {
	/** Cached DAOS file system */
	dfs_t *dd_dfs;
	/** Key prefix of the DFS root directory */
	char   dd_key_root_prefix[DCACHE_KEY_PREF_SIZE];
	/** cache type */
	int    dd_type;
	union {
		/** process local (dram) cache */
		struct {
			/** Hash table holding the cached directories */
			struct d_hash_table  dd_dir_hash;
			/** flag to indicate whether garbage collection is disabled or not */
			bool                 dd_disable_gc;
			/** Garbage collector time-out of a dir-cache record in seconds */
			uint32_t             dd_timeout_rec;
			/** Entry head of the garbage collector list */
			d_list_t             dd_head_gc;
			/** Mutex protecting access to the garbage collector list */
			pthread_mutex_t      dd_mutex_gc;
			/** Size of the garbage collector list */
			uint64_t             dd_count_gc;
			/** Time period of garbage collection */
			uint32_t             dd_period_gc;
			/** Max nr of records to reclaim per garbage collector trigger */
			uint32_t             dd_reclaim_max_gc;
			/** Next Garbage collection date */
			struct timespec      dd_expire_gc;
			/** True iff one thread is running the garbage collection */
			atomic_flag          dd_running_gc;
			/** Destroy a dfs dir-cache */
			destroy_fn_t         destroy_fn;
			/** Return the record of a given location and insert it if needed */
			find_insert_fn_t     find_insert_fn;
			/** Return the record of a given location and insert it if needed */
			find_insert_rel_fn_t find_insert_rel_fn;
			/** Increase the reference counter of a given dir-cache record */
			drec_incref_fn_t     drec_incref_fn;
			/** Decrease the reference counter of a given dir-cache record */
			drec_decref_fn_t     drec_decref_fn;
			/** Delete a given dir-cache record */
			drec_del_at_fn_t     drec_del_at_fn;
			/** Delete the dir-cache record of a given location */
			drec_del_fn_t        drec_del_fn;
		} dh;

		/** Shmem version */
		struct {
			/** shm hash table */
			struct d_shm_ht_loc dd_ht;
		} shm;
	};
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
	return d_list_pop_entry(&dcache->dh.dd_head_gc, dfs_obj_t, dh.dc_entry_gc);
}

static inline int
gc_init_rec(dfs_dcache_t *dcache, dfs_obj_t *rec)
{
	int rc;

	if (dcache->dh.dd_period_gc == 0)
		return -DER_SUCCESS;

	rc = clock_gettime(CLOCK_MONOTONIC_COARSE, &rec->dh.dc_expire_gc);
	if (unlikely(rc != 0))
		return d_errno2der(errno);
	rec->dh.dc_expire_gc.tv_sec += dcache->dh.dd_timeout_rec;

	atomic_flag_clear(&rec->dh.dc_deleted);
	D_INIT_LIST_HEAD(&rec->dh.dc_entry_gc);

	return -DER_SUCCESS;
}

static inline void
gc_add_rec(dfs_dcache_t *dcache, dfs_obj_t *rec)
{
	int rc;

	if (dcache->dh.dd_period_gc == 0)
		return;

	rc = D_MUTEX_LOCK(&dcache->dh.dd_mutex_gc);
	D_ASSERT(rc == 0);

	if (rec->dh.dc_deleted_gc)
		/* NOTE Eventually happen if an entry is added and then removed before the entry has
		 * been added to the GC list*/
		D_GOTO(unlock, rc);

	d_list_add_tail(&rec->dh.dc_entry_gc, &dcache->dh.dd_head_gc);
	++dcache->dh.dd_count_gc;
	D_DEBUG(DB_TRACE, "add record " DF_DK " to GC: count_gc=%" PRIu64 "\n",
		DP_DK(rec->dh.dc_key), dcache->dh.dd_count_gc);

unlock:
	rc = D_MUTEX_UNLOCK(&dcache->dh.dd_mutex_gc);
	D_ASSERT(rc == 0);
}

static inline void
gc_del_rec(dfs_dcache_t *dcache, dfs_obj_t *rec)
{
	int rc;

	if (dcache->dh.dd_period_gc == 0)
		return;

	rc = D_MUTEX_LOCK(&dcache->dh.dd_mutex_gc);
	D_ASSERT(rc == 0);

	if (rec->dh.dc_deleted_gc)
		/* NOTE Eventually happen if an entry is reclaimed and then removed before it has
		 * been removed from the hash table */
		D_GOTO(unlock, rc);

	if (d_list_empty(&rec->dh.dc_entry_gc)) {
		/* NOTE Eventually happen if an entry is added and then removed before the entry has
		 * been added to the GC list*/
		rec->dh.dc_deleted_gc = true;
		D_GOTO(unlock, rc);
	}

	D_ASSERT(dcache->dh.dd_count_gc > 0);
	d_list_del(&rec->dh.dc_entry_gc);
	--dcache->dh.dd_count_gc;
	rec->dh.dc_deleted_gc = true;
	D_DEBUG(DB_TRACE, "remove deleted record " DF_DK " from GC: count_gc=%" PRIu64 "\n",
		DP_DK(rec->dh.dc_key), dcache->dh.dd_count_gc);

unlock:
	rc = D_MUTEX_UNLOCK(&dcache->dh.dd_mutex_gc);
	D_ASSERT(rc == 0);
}

static inline int
gc_reclaim(dfs_dcache_t *dcache)
{
	struct timespec now;
	uint32_t        reclaim_count;
	int             rc;

	if (dcache->dh.dd_period_gc == 0)
		D_GOTO(out, rc = -DER_SUCCESS);

	if (atomic_flag_test_and_set(&dcache->dh.dd_running_gc))
		D_GOTO(out, rc = -DER_SUCCESS);

	rc = clock_gettime(CLOCK_MONOTONIC_COARSE, &now);
	if (unlikely(rc != 0))
		D_GOTO(out_unset, rc = d_errno2der(errno));

	if (time_cmp(&dcache->dh.dd_expire_gc, &now) > 0)
		D_GOTO(out_unset, rc = -DER_SUCCESS);

	D_DEBUG(DB_TRACE, "start GC reclaim at " DF_TS ": GC size=%" PRIu64 "\n", DP_TS(now),
		dcache->dh.dd_count_gc);

	reclaim_count = 0;
	while (reclaim_count < dcache->dh.dd_reclaim_max_gc) {
		dfs_obj_t *rec;

		rc = D_MUTEX_LOCK(&dcache->dh.dd_mutex_gc);
		D_ASSERT(rc == 0);

		if ((rec = gc_pop_rec(dcache)) == NULL) {
			rc = D_MUTEX_UNLOCK(&dcache->dh.dd_mutex_gc);
			D_ASSERT(rc == 0);
			break;
		}

		if (time_cmp(&rec->dh.dc_expire_gc, &now) > 0) {
			d_list_add(&rec->dh.dc_entry_gc, &dcache->dh.dd_head_gc);
			rc = D_MUTEX_UNLOCK(&dcache->dh.dd_mutex_gc);
			D_ASSERT(rc == 0);
			break;
		}

		D_ASSERT(dcache->dh.dd_count_gc > 0);
		--dcache->dh.dd_count_gc;
		rec->dh.dc_deleted_gc = true;

		rc = D_MUTEX_UNLOCK(&dcache->dh.dd_mutex_gc);
		D_ASSERT(rc == 0);

		D_DEBUG(DB_TRACE, "remove expired record " DF_DK " from GC: expire=" DF_TS "\n",
			DP_DK(rec->dh.dc_key), DP_TS(rec->dh.dc_expire_gc));

		if (!atomic_flag_test_and_set(&rec->dh.dc_deleted))
			d_hash_rec_delete_at(&dcache->dh.dd_dir_hash, &rec->dh.dc_entry);

		++reclaim_count;
	}

	if (reclaim_count >= dcache->dh.dd_reclaim_max_gc) {
		D_DEBUG(DB_TRACE, "yield GC reclaim: GC size=%" PRIu64 "\n",
			dcache->dh.dd_count_gc);
		D_GOTO(out_unset, rc = -DER_SUCCESS);
	}

	rc = clock_gettime(CLOCK_MONOTONIC_COARSE, &now);
	if (unlikely(rc != 0))
		D_GOTO(out_unset, rc = d_errno2der(errno));
	dcache->dh.dd_expire_gc.tv_sec  = now.tv_sec + dcache->dh.dd_period_gc;
	dcache->dh.dd_expire_gc.tv_nsec = now.tv_nsec;

	D_DEBUG(DB_TRACE, "stop GC reclaim at " DF_TS ": GC size=%" PRIu64 "\n", DP_TS(now),
		dcache->dh.dd_count_gc);

out_unset:
	atomic_flag_clear(&dcache->dh.dd_running_gc);
out:
	return rc;
}

static inline dfs_obj_t *
dlist2drec(d_list_t *rlink)
{
	return d_list_entry(rlink, dfs_obj_t, dh.dc_entry);
}

static bool
dcache_key_cmp(struct d_hash_table *htable, d_list_t *rlink, const void *key, unsigned int key_len)
{
	dfs_obj_t *rec;

	rec = dlist2drec(rlink);
	if (rec->dh.dc_key_len != key_len)
		return false;
	D_ASSERT(rec->dh.dc_key[key_len] == '\0');
	D_ASSERT(((char *)key)[key_len] == '\0');

	return strncmp(rec->dh.dc_key, (const char *)key, key_len) == 0;
}

static void
dcache_rec_free(struct d_hash_table *htable, d_list_t *rlink)
{
	int           rc;
	dfs_obj_t    *rec;

	rec = dlist2drec(rlink);
	D_DEBUG(DB_TRACE, "delete record " DF_DK " (ref=%u)", DP_DK(rec->dh.dc_key),
		atomic_load(&rec->dh.dc_ref));
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
	oldref = atomic_fetch_add(&rec->dh.dc_ref, 1);
	D_DEBUG(DB_TRACE, "increment ref counter of record " DF_DK " from %u to %u",
		DP_DK(rec->dh.dc_key), oldref, oldref + 1);
}

static bool
dcache_rec_decref(struct d_hash_table *htable, d_list_t *rlink)
{
	dfs_obj_t    *rec;
	uint32_t      oldref;

	rec    = dlist2drec(rlink);
	oldref = atomic_fetch_sub(&rec->dh.dc_ref, 1);
	D_DEBUG(DB_TRACE, "decrement ref counter of record " DF_DK " from %u to %u",
		DP_DK(rec->dh.dc_key), oldref, oldref - 1);
	D_ASSERT(oldref >= 1);
	return oldref == 1;
}

static uint32_t
dcache_rec_hash(struct d_hash_table *htable, d_list_t *rlink)
{
	dfs_obj_t *rec;

	rec = dlist2drec(rlink);
	return d_hash_string_u32(rec->dh.dc_key, rec->dh.dc_key_len);
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
	void       *val;
	daos_size_t val_size;
	int         rc;

	/** for local cache, just duplicate the entry and store the pointer */
	if (dcache->dd_type == DFS_CACHE_DRAM) {
		dfs_obj_t *rec;

		rc = dup_int(dcache->dd_dfs, &dcache->dd_dfs->root, O_RDWR, &rec,
			     DCACHE_KEY_PREF_SIZE);
		if (rc)
			return rc;
		rec->dc = dcache;
		atomic_init(&rec->dh.dc_ref, 0);
		atomic_flag_clear(&rec->dh.dc_deleted);
		memcpy(&rec->dh.dc_key_child_prefix[0], &dcache->dd_key_root_prefix[0],
		       DCACHE_KEY_PREF_SIZE);
		memcpy(&rec->dh.dc_key[0], &dcache->dd_key_root_prefix[0], DCACHE_KEY_PREF_SIZE);
		rec->dh.dc_key_len = DCACHE_KEY_PREF_SIZE - 1;

		rc = d_hash_rec_insert(&dcache->dh.dd_dir_hash, rec->dh.dc_key, rec->dh.dc_key_len,
				       &rec->dh.dc_entry, true);
		if (rc)
			release_int(rec);
		return rc;
	}

	/** for shmem cache, serialize the object and insert the serialized form */
	rc = dfs_obj_serialize(&dcache->dd_dfs->root, NULL, &val_size);
	if (rc)
		return rc;

	D_ALLOC(val, val_size);
	if (val == NULL)
		return ENOMEM;

	rc = dfs_obj_serialize(&dcache->dd_dfs->root, val, &val_size);
	if (rc)
		return rc;

	shm_ht_rec_find_insert(&dcache->shm.dd_ht, &dcache->dd_key_root_prefix[0],
			       DCACHE_KEY_PREF_SIZE - 1, val, val_size,
			       &dcache->dd_dfs->root.shm.rec_loc, &rc);
	/* val was copied into shm hash table record, so it is not needed any more. */
	D_FREE(val);
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
	dcache_tmp->dd_type               = DFS_CACHE_DRAM;
	dcache_tmp->dh.destroy_fn         = dcache_destroy_act;
	dcache_tmp->dh.find_insert_fn     = dcache_find_insert_act;
	dcache_tmp->dh.find_insert_rel_fn = dcache_find_insert_rel_act;
	dcache_tmp->dh.drec_incref_fn     = drec_incref_act;
	dcache_tmp->dh.drec_decref_fn     = drec_decref_act;
	dcache_tmp->dh.drec_del_at_fn     = drec_del_at_act;
	dcache_tmp->dh.drec_del_fn        = drec_del_act;

	if (rec_timeout == 0) {
		dcache_tmp->dh.dd_disable_gc = true;
	} else {
		dcache_tmp->dh.dd_timeout_rec    = rec_timeout;
		dcache_tmp->dh.dd_period_gc      = gc_period;
		dcache_tmp->dh.dd_reclaim_max_gc = gc_reclaim_max;
		dcache_tmp->dh.dd_count_gc       = 0;
		dcache_tmp->dh.dd_disable_gc     = false;

		rc = D_MUTEX_INIT(&dcache_tmp->dh.dd_mutex_gc, NULL);
		if (rc != 0)
			D_GOTO(error_alloc, daos_errno2der(rc));
		atomic_flag_clear(&dcache_tmp->dh.dd_running_gc);
		D_INIT_LIST_HEAD(&dcache_tmp->dh.dd_head_gc);

		rc = clock_gettime(CLOCK_MONOTONIC_COARSE, &dcache_tmp->dh.dd_expire_gc);
		if (unlikely(rc != 0))
			D_GOTO(error_mutex, rc = d_errno2der(errno));
		dcache_tmp->dh.dd_expire_gc.tv_sec += dcache_tmp->dh.dd_period_gc;
	}

	rc = snprintf(&dcache_tmp->dd_key_root_prefix[0], DCACHE_KEY_PREF_SIZE,
		      "%016" PRIx64 "-%016" PRIx64 ":", dfs->root.oid.hi, dfs->root.oid.lo);
	D_ASSERT(rc == DCACHE_KEY_PREF_SIZE - 1);

	rc = d_hash_table_create_inplace(D_HASH_FT_MUTEX | D_HASH_FT_LRU, bits, NULL,
					 &dcache_hash_ops, &dcache_tmp->dh.dd_dir_hash);
	if (rc != 0)
		D_GOTO(error_mutex, rc = daos_der2errno(rc));

	rc = dcache_add_root(dcache_tmp);
	if (rc != 0)
		D_GOTO(error_htable, rc);

	dfs->dcache = dcache_tmp;
	return 0;

error_htable:
	d_hash_table_destroy_inplace(&dcache_tmp->dh.dd_dir_hash, true);
error_mutex:
	if (dcache_tmp->dh.dd_disable_gc == false)
		D_MUTEX_DESTROY(&dcache_tmp->dh.dd_mutex_gc);
error_alloc:
	D_FREE(dcache_tmp);
	return rc;
}

static int
dcache_destroy_act(dfs_dcache_t *dcache)
{
	d_list_t *rlink;
	int       rc;

	while ((rlink = d_hash_rec_first(&dcache->dh.dd_dir_hash)) != NULL) {
		dfs_obj_t *rec;
		bool       deleted;

		rec = dlist2drec(rlink);
		/* '/' is never in the GC list */
		if (!dcache->dh.dd_disable_gc && rec->dh.dc_key_len != DCACHE_KEY_PREF_SIZE - 1)
			gc_del_rec(dcache, rec);
		D_ASSERT(atomic_load(&rec->dh.dc_ref) == 1);
		deleted = d_hash_rec_delete_at(&dcache->dh.dd_dir_hash, rlink);
		D_ASSERT(deleted);
	}
	D_ASSERT(dcache->dh.dd_count_gc == 0);

	rc = d_hash_table_destroy_inplace(&dcache->dh.dd_dir_hash, false);
	if (rc != 0) {
		DL_ERROR(rc, "d_hash_table_destroy_inplace() failed");
		return daos_der2errno(rc);
	}

	if (!dcache->dh.dd_disable_gc) {
		rc = D_MUTEX_DESTROY(&dcache->dh.dd_mutex_gc);
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

	rlink = d_hash_rec_find(&dcache->dh.dd_dir_hash, key, key_len);
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
	atomic_init(&obj->dh.dc_ref, 1);
	atomic_flag_clear(&obj->dh.dc_deleted);
	rc = snprintf(&obj->dh.dc_key_child_prefix[0], DCACHE_KEY_PREF_SIZE,
		      "%016" PRIx64 "-%016" PRIx64 ":", obj->oid.hi, obj->oid.lo);
	D_ASSERT(rc == DCACHE_KEY_PREF_SIZE - 1);
	obj->dh.dc_key_len = key_len;
	memcpy(obj->dh.dc_key, key, key_len + 1);

	/** for shmem cache, serialize the object and insert the serialized form */
	if (dcache->dd_type == DFS_CACHE_SHM) {
		void       *val;
		daos_size_t val_size;

		rc = dfs_obj_serialize(obj, NULL, &val_size);
		if (rc)
			goto err;

		D_ALLOC(val, val_size);
		if (val == NULL)
			D_GOTO(err, rc = ENOMEM);

		rc = dfs_obj_serialize(obj, val, &val_size);
		if (rc) {
			D_FREE(val);
			goto err;
		}

		shm_ht_rec_find_insert(&dcache->shm.dd_ht, key,	key_len, val, val_size, &obj->shm.rec_loc,
				       &rc);
		/* val was copied into shm hash table record, so it is not needed any more. */
		D_FREE(val);
		if (rc)
			dfs_release(obj);
		*rec = obj;
		return rc;
	}

	if (!dcache->dh.dd_disable_gc) {
		rc = gc_init_rec(dcache, obj);
		if (rc != 0) {
			dfs_release(obj);
			return rc;
		}
	}

	rlink = d_hash_rec_find_insert(&dcache->dh.dd_dir_hash, obj->dh.dc_key, key_len,
				       &obj->dh.dc_entry);
	if (rlink == &obj->dh.dc_entry) {
		D_DEBUG(DB_TRACE, "add record " DF_DK " with ref counter %u", DP_DK(obj->dh.dc_key),
			obj->dh.dc_ref);
		if (!dcache->dh.dd_disable_gc)
			gc_add_rec(dcache, obj);
	} else {
		dcache_rec_free(&dcache->dh.dd_dir_hash, &obj->dh.dc_entry);
		obj = dlist2drec(rlink);
	}

	*rec = obj;
	return 0;
err:
	dfs_release(obj);
	return rc;
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
				rc = entry_stat(dcache->dd_dfs, dcache->dd_dfs->th, parent->oh,
						name, len, NULL, true, stbuf, NULL);
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
	if (!dcache->dh.dd_disable_gc) {
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

		key_prefix = &rec->dh.dc_key_child_prefix[0];
		parent     = rec;
	}

	if (stbuf && !skip_stat) {
		if (rec->dc_stated) {
			memcpy(stbuf, &rec->dc_stbuf, sizeof(struct stat));
		} else {
			rc = entry_stat(dcache->dd_dfs, dcache->dd_dfs->th, parent->oh, rec->name,
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
	if (!dcache->dh.dd_disable_gc) {
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
	d_hash_rec_addref(&dcache->dh.dd_dir_hash, &rec->dh.dc_entry);
}

static void
drec_decref_act(dfs_dcache_t *dcache, dfs_obj_t *rec)
{
	d_hash_rec_decref(&dcache->dh.dd_dir_hash, &rec->dh.dc_entry);
}

static void
drec_del_at_act(dfs_dcache_t *dcache, dfs_obj_t *rec)
{
	if (!dcache->dh.dd_disable_gc)
		gc_del_rec(dcache, rec);
	d_hash_rec_decref(&dcache->dh.dd_dir_hash, &rec->dh.dc_entry);
	if (!atomic_flag_test_and_set(&rec->dh.dc_deleted))
		d_hash_rec_delete_at(&dcache->dh.dd_dir_hash, &rec->dh.dc_entry);
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
	memcpy(key, parent->dh.dc_key_child_prefix, key_prefix_len);
	memcpy(key + key_prefix_len, bname, bname_len);
	key[key_len] = '\0';

	rlink = d_hash_rec_find(&dcache->dh.dd_dir_hash, key, key_len);
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

	dcache_tmp->dh.destroy_fn         = dcache_destroy_dact;
	dcache_tmp->dh.find_insert_fn     = dcache_find_insert_dact;
	dcache_tmp->dh.find_insert_rel_fn = dcache_find_insert_rel_dact;
	dcache_tmp->dh.drec_incref_fn     = NULL;
	dcache_tmp->dh.drec_decref_fn     = drec_del_at_dact;
	dcache_tmp->dh.drec_del_at_fn     = drec_del_at_dact;
	dcache_tmp->dh.drec_del_fn        = NULL;
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

static int
dcache_create_shm(dfs_t *dfs, uint32_t bits, uint32_t rec_timeout, uint32_t gc_period,
		  uint32_t gc_reclaim_max)
{
	dfs_dcache_t *dcache_tmp;
	uuid_t        pool_uuid;
	uuid_t        cont_uuid;
	char          ht_name[DAOS_UUID_STR_SIZE * 2 + 1];
	int           rc;

	rc = shm_init();
	if (rc)
		return daos_der2errno(rc);

	D_ALLOC_PTR(dcache_tmp);
	if (dcache_tmp == NULL)
		D_GOTO(err_shm, rc = ENOMEM);

	dcache_tmp->dd_dfs  = dfs;
	dcache_tmp->dd_type = DFS_CACHE_SHM;

	/** create / open a hash table with the pool.cont name */
	rc = dc_pool_hdl2uuid(dfs->poh, NULL, &pool_uuid);
	if (rc)
		D_GOTO(err_dcache, rc = daos_der2errno(rc));

	rc = dc_cont_hdl2uuid(dfs->coh, NULL, &cont_uuid);
	if (rc)
		D_GOTO(err_dcache, rc = daos_der2errno(rc));

	uuid_unparse(pool_uuid, ht_name);
	ht_name[36] = '.';
	uuid_unparse(cont_uuid, ht_name + DAOS_UUID_STR_SIZE);

	rc = shm_ht_create(ht_name, bits, 4, &dcache_tmp->shm.dd_ht);
	if (rc != 0)
		D_GOTO(err_dcache, rc = daos_der2errno(rc));

	rc = snprintf(&dcache_tmp->dd_key_root_prefix[0], DCACHE_KEY_PREF_SIZE,
		      "%016" PRIx64 "-%016" PRIx64 ":", dfs->root.oid.hi, dfs->root.oid.lo);
	D_ASSERT(rc == DCACHE_KEY_PREF_SIZE - 1);

	rc = dcache_add_root(dcache_tmp);
	if (rc != 0)
		D_GOTO(err_ht, rc = daos_der2errno(rc));

	dfs->dcache = dcache_tmp;
	return 0;

err_ht:
	shm_ht_decref(&dcache_tmp->shm.dd_ht);
err_dcache:
	D_FREE(dcache_tmp);
err_shm:
	shm_fini();
	return rc;
}

int
dcache_create(dfs_t *dfs, int type, uint32_t bits, uint32_t rec_timeout, uint32_t gc_period,
	      uint32_t gc_reclaim_max)
{
	D_ASSERT(dfs);
	if (bits == 0)
		return dcache_create_dact(dfs);
	if (type == DFS_CACHE_DRAM)
		return dcache_create_act(dfs, bits, rec_timeout, gc_period, gc_reclaim_max);
	else
		return dcache_create_shm(dfs, bits, rec_timeout, gc_period, gc_reclaim_max);
}

int
dcache_destroy(dfs_t *dfs)
{
	D_ASSERT(dfs->dcache != NULL);
	if (dfs->dcache->dd_type == DFS_CACHE_SHM) {
		int rc;

		/* decrease the ht record reference of root */
		rc = shm_ht_rec_decref(&dfs->dcache->dd_dfs->root.shm.rec_loc);
		if (rc)
			D_ERROR("shm_ht_rec_decref() failed: " DF_RC "\n", DP_RC(rc));

		rc = shm_ht_decref(&dfs->dcache->shm.dd_ht);
		if (rc)
			D_ERROR("shm_ht_rec_decref() failed: " DF_RC "\n", DP_RC(rc));
		return daos_der2errno(rc);
	}

	D_ASSERT(dfs->dcache->dh.destroy_fn != NULL);
	return dfs->dcache->dh.destroy_fn(dfs->dcache);
}

int
dcache_find_insert(dfs_t *dfs, char *path, size_t path_len, int flags, dfs_obj_t **_rec,
		   mode_t *mode, struct stat *stbuf)
{
	const size_t key_prefix_len = DCACHE_KEY_PREF_SIZE - 1;
	dfs_obj_t   *rec;
	dfs_obj_t   *parent;
	char        *key;
	char        *key_prefix;
	char        *name;
	size_t       name_len;
	bool         skip_stat = false;
	int          rc;

	D_ASSERT(_rec != NULL);
	D_ASSERT(dfs->dcache != NULL);
	D_ASSERT(path != NULL);

	if (dfs->dcache->dd_type == DFS_CACHE_DRAM) {
		D_ASSERT(dfs->dcache->dh.find_insert_fn != NULL);
		return dfs->dcache->dh.find_insert_fn(dfs->dcache, path, path_len, flags, _rec,
						      mode, stbuf);
	}

	D_ALLOC(key, key_prefix_len + path_len + 1);
	if (key == NULL)
		return -DER_NOMEM;

	name       = path;
	name_len   = 0;
	key_prefix = dfs->dcache->dd_key_root_prefix;
	parent     = NULL;

	for (;;) {
		size_t                  key_len;
		struct d_shm_ht_rec_loc rec_loc;
		char                   *value;

		memcpy(key, key_prefix, key_prefix_len);
		memcpy(key + key_prefix_len, name, name_len);
		key_len      = key_prefix_len + name_len;
		key[key_len] = '\0';

		value =
		    (char *)shm_ht_rec_find(&dfs->dcache->shm.dd_ht, key, key_len, &rec_loc, &rc);
		D_DEBUG(DB_TRACE, "dcache %s: path=" DF_PATH ", key=" DF_DK "\n",
			(value == NULL) ? "miss" : "hit", DP_PATH(path), DP_DK(key));
		if (rc)
			D_GOTO(out, rc = daos_der2errno(rc));
		if (value == NULL) {
			char tmp;

			D_ASSERT(name_len > 0);
			D_ASSERT(parent != NULL);

			tmp            = name[name_len];
			name[name_len] = '\0';

			/** don't pass stbuf and mode if this is not the last entry */
			if (name + name_len == path + path_len) {
				rc = dcache_add(dfs->dcache, parent, name, name_len, key, key_len,
						&rec, mode, stbuf);
				/** stbuf and mode are filled already if valid, so skip later */
				skip_stat = true;
			} else {
				rc = dcache_add(dfs->dcache, parent, name, name_len, key, key_len,
						&rec, NULL, NULL);
			}
			name[name_len] = tmp;
			if (rc) {
				drec_decref(dfs->dcache, parent);
				D_GOTO(out, rc);
			}
		} else {
			D_ALLOC_PTR(rec);
			if (rec == NULL)
				D_GOTO(out, rc = ENOMEM);

			rc = dfs_obj_deserialize(value, strlen(value), rec);
			if (rc)
				D_GOTO(out, rc);
		}
		D_ASSERT(rec != NULL);

		if (parent != NULL)
			drec_decref(dfs->dcache, parent);

		// NOTE skip '/' character
		name += name_len + 1;
		name_len = 0;
		while (name + name_len < path + path_len && name[name_len] != '/')
			++name_len;
		if (name_len == 0) {
			/** handle following symlinks outside of the dcache */
			if (S_ISLNK(rec->mode) && !(flags & O_NOFOLLOW)) {
				dfs_obj_t *sym;

				rc = lookup_rel_path(dfs->dcache->dd_dfs, parent, rec->value, flags,
						     &sym, mode, stbuf, 0);
				drec_decref(dfs->dcache, rec);
				if (rc)
					D_GOTO(out, rc);
				rec = sym;
				D_GOTO(done, rc);
			}
			break;
		}

		key_prefix = &rec->dh.dc_key_child_prefix[0];
		parent     = rec;
	}

	if (stbuf && !skip_stat) {
		if (rec->dc_stated) {
			memcpy(stbuf, &rec->dc_stbuf, sizeof(struct stat));
		} else {
			rc = entry_stat(dfs, dfs->th, parent->oh, rec->name, strlen(rec->name), rec,
					true, stbuf, NULL);
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
out:
	D_FREE(key);
	return rc;
}

int
dcache_find_insert_rel(dfs_t *dfs, dfs_obj_t *parent, const char *name, size_t len, int flags,
		       dfs_obj_t **_rec, mode_t *mode, struct stat *stbuf)
{
	const size_t            key_prefix_len = DCACHE_KEY_PREF_SIZE - 1;
	dfs_obj_t              *rec;
	char                    key[DCACHE_KEY_PREF_SIZE + DFS_MAX_NAME];
	char                   *key_prefix;
	size_t                  key_len;
	char                   *value;
	struct d_shm_ht_rec_loc rec_loc;
	int                     rc;

	D_ASSERT(dfs->dcache != NULL);
	D_ASSERT(name != NULL);

	if (dfs->dcache->dd_type == DFS_CACHE_DRAM) {
		D_ASSERT(dfs->dcache->dh.find_insert_rel_fn != NULL);
		return dfs->dcache->dh.find_insert_rel_fn(dfs->dcache, parent, name, len, flags,
							  _rec, mode, stbuf);
	}

	key_prefix = dfs->dcache->dd_key_root_prefix;
	memcpy(key, key_prefix, key_prefix_len);
	memcpy(key + key_prefix_len, name, len);
	key_len      = key_prefix_len + len;
	key[key_len] = '\0';

	value = (char *)shm_ht_rec_find(&dfs->dcache->shm.dd_ht, key, key_len, &rec_loc, &rc);
	D_DEBUG(DB_TRACE, "dcache %s: name=" DF_PATH ", key=" DF_DK "\n",
		(value == NULL) ? "miss" : "hit", DP_PATH(name), DP_DK(key));
	if (rc)
		D_GOTO(out, rc = daos_der2errno(rc));
	if (value == NULL) {
		rc = dcache_add(dfs->dcache, parent, name, len, key, key_len, &rec, mode, stbuf);
		if (rc)
			D_GOTO(out, rc);
	} else {
		D_ALLOC_PTR(rec);
		if (rec == NULL)
			D_GOTO(out, rc = ENOMEM);

		rc = dfs_obj_deserialize(value, strlen(value), rec);
		if (rc)
			D_GOTO(out, rc);
		memcpy(&rec->shm.rec_loc, &rec_loc, sizeof(rec_loc));

		/** handle following symlinks outside of the dcache */
		if (S_ISLNK(rec->mode) && !(flags & O_NOFOLLOW)) {
			dfs_obj_t *sym;

			rc = lookup_rel_path(dfs, parent, rec->value, flags, &sym, mode, stbuf, 0);
			drec_decref(dfs->dcache, rec);
			if (rc)
				D_GOTO(out, rc);
			rec = sym;
			D_GOTO(done, rc);
		}
		if (stbuf) {
			if (rec->dc_stated) {
				memcpy(stbuf, &rec->dc_stbuf, sizeof(struct stat));
			} else {
				rc = entry_stat(dfs, dfs->th, parent->oh, name, len, NULL, true,
						stbuf, NULL);
				if (rc != 0)
					D_GOTO(out, rc);
				memcpy(&rec->dc_stbuf, stbuf, sizeof(struct stat));
				rec->dc_stated = true;
			}
		}
		if (mode)
			*mode = rec->mode;
		rec->dc = dfs->dcache;
		rc = 0;
	}

done:
	D_ASSERT(rec != NULL);
	*_rec = rec;
out:
	return rc;
}

void
drec_incref(dfs_dcache_t *dcache, dfs_obj_t *rec)
{
	if (rec == NULL)
		return;
	D_ASSERT(dcache != NULL);

	if (dcache->dd_type == DFS_CACHE_SHM)
		D_ASSERT(0);

	if (dcache->dh.drec_incref_fn == NULL)
		return;
	dcache->dh.drec_incref_fn(dcache, rec);
}

void
drec_decref(dfs_dcache_t *dcache, dfs_obj_t *rec)
{
	if (rec == NULL)
		return;
	D_ASSERT(dcache != NULL);

	if (dcache->dd_type == DFS_CACHE_SHM) {
		shm_ht_rec_decref(&rec->shm.rec_loc);
	} else {
		D_ASSERT(dcache->dh.drec_decref_fn != NULL);
		dcache->dh.drec_decref_fn(dcache, rec);
	}
}

void
drec_del_at(dfs_dcache_t *dcache, dfs_obj_t *rec)
{
	if (rec == NULL)
		return;
	D_ASSERT(dcache != NULL);
	D_ASSERT(dcache->dh.drec_del_at_fn != NULL);

	dcache->dh.drec_del_at_fn(dcache, rec);
}

int
drec_del(dfs_dcache_t *dcache, char *path, dfs_obj_t *parent)
{
	D_ASSERT(dcache != NULL);
	if (dcache->dh.drec_del_fn == NULL)
		return -DER_SUCCESS;

	return dcache->dh.drec_del_fn(dcache, path, parent);
}

// dcache_readdir(dfs_dcache_t *dcache, dfs_obj_t *obj, dfs_dir_anchor_t *anchor, struct dirent dir)
// {}
