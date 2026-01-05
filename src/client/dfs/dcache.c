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
#include <gurt/shm_utils.h>
#include <daos/common.h>

#include "dfs_internal.h"

/** DFS dentry cache */
struct dfs_dcache {
	/** Cached DAOS file system */
	dfs_t           *dd_dfs;
	/** shm lru cache */
	shm_lru_cache_t *d_shm_lru_dentry;
	/* the hash value of pool uuid & cont uuid */
	uint64_t         dd_pool_cont_hash;
};

/** serialize the object and insert the serialized form */
static inline int
dcache_add_root(dfs_dcache_t *dcache)
{
	int         rc;
	void       *val = NULL;
	daos_size_t val_size;
	char        key[DCACHE_KEY_PREF_SIZE];
	uint64_t   *ptr_key = (uint64_t *)key;

	rc = dfs_obj_serialize(&dcache->dd_dfs->root, NULL, &val_size);
	if (rc)
		return rc;

	D_ALLOC(val, val_size);
	if (val == NULL)
		return ENOMEM;

	rc = dfs_obj_serialize(&dcache->dd_dfs->root, val, &val_size);
	if (rc)
		goto out;

	ptr_key[0] = dcache->dd_pool_cont_hash;
	ptr_key[1] = dcache->dd_dfs->root.oid.lo;
	ptr_key[2] = dcache->dd_dfs->root.oid.hi;
	rc =
	    shm_lru_put(dcache->d_shm_lru_dentry, (void *)key, DCACHE_KEY_PREF_SIZE, val, val_size);
	/* val was copied into shm LRU record, so it is not needed any more. */

out:
	D_FREE(val);
	return rc;
}

static inline int
dcache_add(dfs_dcache_t *dcache, dfs_obj_t *parent, const char *name, size_t len, const char *key,
	   size_t key_len, dfs_obj_t **rec, mode_t *mode, struct stat *stbuf)
{
	daos_size_t val_size;
	dfs_obj_t  *obj = NULL;
	void       *val = NULL;
	int         rc;

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

	/* serialize the object and insert the serialized form */
	rc = dfs_obj_serialize(obj, NULL, &val_size);
	if (rc)
		D_GOTO(err, rc);

	D_ALLOC(val, val_size);
	if (val == NULL)
		D_GOTO(err, rc = ENOMEM);

	rc = dfs_obj_serialize(obj, val, &val_size);
	if (rc)
		D_GOTO(err, rc);

	rc = shm_lru_put(dcache->d_shm_lru_dentry, (void *)key, key_len, val, val_size);
	/* val was copied into shm hash table record, so it is not needed any more. */
	D_FREE(val);
	if (rc)
		dfs_release(obj);
	else
		*rec = obj;
	return rc;

err:
	D_FREE(val);
	dfs_release(obj);
	return rc;
}

int
dcache_create(dfs_t *dfs)
{
	dfs_dcache_t *dcache_tmp;
	/* pool_cont_uuid will hold pool uuid and cont uuid */
	uuid_t        pool_cont_uuid[2];
	int           rc;

	D_ASSERT(dfs);

	rc = shm_init();
	if (rc)
		return daos_der2errno(rc);

	D_ALLOC_PTR(dcache_tmp);
	if (dcache_tmp == NULL)
		D_GOTO(err_shm, rc = ENOMEM);

	dcache_tmp->dd_dfs = dfs;

	/** calculate a hash with the pool and cont uuid */
	rc = dc_pool_hdl2uuid(dfs->poh, NULL, &pool_cont_uuid[0]);
	if (rc)
		D_GOTO(err_dcache, rc = daos_der2errno(rc));

	rc = dc_cont_hdl2uuid(dfs->coh, NULL, &pool_cont_uuid[1]);
	if (rc)
		D_GOTO(err_dcache, rc = daos_der2errno(rc));

	/* acquire the pointer of the existing shm LRU cache for dentry */
	dcache_tmp->d_shm_lru_dentry = shm_lru_get_cache(CACHE_DENTRY);
	D_ASSERT(dcache_tmp->d_shm_lru_dentry != NULL);
	dcache_tmp->dd_pool_cont_hash =
	    d_hash_murmur64((const unsigned char *)pool_cont_uuid, sizeof(uuid_t) * 2, 0);

	rc = dcache_add_root(dcache_tmp);
	if (rc != 0)
		D_GOTO(err_dcache, rc = daos_der2errno(rc));

	dfs->dcache = dcache_tmp;
	return 0;

err_dcache:
	D_FREE(dcache_tmp);
err_shm:
	shm_fini();
	return rc;
}

int
dcache_destroy(dfs_t *dfs)
{
	D_ASSERT(dfs->dcache != NULL);
	D_FREE(dfs->dcache);
	return 0;
}

int
dcache_find_insert(dfs_t *dfs, char *path, size_t path_len, int flags, dfs_obj_t **_rec,
		   mode_t *mode, struct stat *stbuf)
{
	dfs_dcache_t   *dcache;
	dfs_obj_t      *rec;
	dfs_obj_t      *parent;
	char            key[DCACHE_KEY_PREF_SIZE + DFS_MAX_NAME];
	char           *name;
	size_t          name_len;
	bool            skip_stat = false;
	int             rc;
	uint64_t       *ptr_key = (uint64_t *)key;
	size_t          key_len;
	shm_lru_node_t *node_found = NULL;
	char           *value;
	size_t          val_size;

	D_ASSERT(_rec != NULL);
	D_ASSERT(dfs->dcache != NULL);
	D_ASSERT(path != NULL);
	D_ASSERT(path_len > 0);

	dcache     = dfs->dcache;
	name       = path;
	name_len   = 0;
	parent     = NULL;
	ptr_key[0] = dcache->dd_pool_cont_hash;
	ptr_key[1] = dfs->root.oid.lo;
	ptr_key[2] = dfs->root.oid.hi;

	for (;;) {
		rec = NULL;
		if (node_found) {
			shm_lru_node_dec_ref(node_found);
			node_found = NULL;
		}

		memcpy(key + DCACHE_KEY_PREF_SIZE, name, name_len);
		key_len = DCACHE_KEY_PREF_SIZE + name_len;
		rc      = shm_lru_get(dcache->d_shm_lru_dentry, key, key_len, &node_found,
				      (void **)&value);
		D_DEBUG(DB_TRACE, "dentry cache %s: path=" DF_PATH "\n", (rc != 0) ? "miss" : "hit",
			DP_PATH(path));
		if (rc == ENOENT) {
			/* record is not found in cache */
			char tmp;

			if (name_len == 0) {
				/* root is not found in cache, then add root first */
				dcache_add_root(dcache);
				continue;
			}

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
			if (rc)
				D_GOTO(err, rc);
		} else {
			D_ALLOC_PTR(rec);
			if (rec == NULL)
				D_GOTO(err, rc = ENOMEM);

			rc = dfs_obj_deserialize(dfs, flags, value, rec);
			if (rc)
				D_GOTO(err, rc);
		}
		D_ASSERT(rec != NULL);

		// NOTE skip '/' character
		name += name_len + 1;
		name_len = 0;
		while (name + name_len < path + path_len && name[name_len] != '/')
			++name_len;
		if (name_len == 0) {
			/** handle following symlinks outside of the dcache */
			if (S_ISLNK(rec->mode) && !(flags & O_NOFOLLOW)) {
				dfs_obj_t *sym;

				rc = lookup_rel_path(dfs, parent, rec->value, flags, &sym, mode,
						     stbuf, 0);
				if (rc)
					D_GOTO(err, rc);
				D_FREE(rec);
				rec = sym;
				D_GOTO(done, rc);
			}
			break;
		}

		parent     = rec;
		ptr_key[1] = parent->oid.lo;
		ptr_key[2] = parent->oid.hi;
	}

	if (stbuf && !skip_stat) {
		if (rec->dc_stated) {
			memcpy(stbuf, &rec->dc_stbuf, sizeof(struct stat));
		} else {
			rc = entry_stat(dfs, dfs->th, parent->oh, rec->name, strlen(rec->name), rec,
					true, stbuf, NULL);
			if (rc != 0)
				D_GOTO(err, rc);
			memcpy(&rec->dc_stbuf, stbuf, sizeof(struct stat));
			rec->dc_stated = true;
			/* stat and dc_stated are updated, need to update the record in cache too.
			 * the data buffer size is stored as an integer at the very beginning of
			 * data.
			 */
			val_size = *((size_t *)value) & 0xFFFFFFFF;
			rc       = dfs_obj_serialize(rec, (uint8_t *)value, &val_size);
			if (rc != 0)
				D_GOTO(err, rc);
		}
	}
	if (mode && !skip_stat)
		*mode = rec->mode;

done:
	*_rec = rec;
	if (node_found)
		shm_lru_node_dec_ref(node_found);
	return rc;

err:
	D_FREE(rec);
	if (node_found)
		shm_lru_node_dec_ref(node_found);
	return rc;
}

int
dcache_find_insert_rel(dfs_t *dfs, dfs_obj_t *parent, const char *name, size_t len, int flags,
		       dfs_obj_t **_rec, mode_t *mode, struct stat *stbuf)
{
	dfs_dcache_t   *dcache;
	dfs_obj_t      *rec = NULL;
	char            key[DCACHE_KEY_PREF_SIZE + DFS_MAX_NAME];
	size_t          key_len;
	char           *value;
	shm_lru_node_t *node_found = NULL;
	int             rc;
	size_t          val_size;
	uint64_t       *ptr_key;

	D_ASSERT(dfs->dcache != NULL);
	D_ASSERT(name != NULL);

	dcache     = dfs->dcache;
	ptr_key    = (uint64_t *)key;
	ptr_key[0] = dcache->dd_pool_cont_hash;
	if (parent) {
		ptr_key[1] = parent->oid.lo;
		ptr_key[2] = parent->oid.hi;
	} else {
		/* item under root dir */
		ptr_key[1] = dfs->root.oid.lo;
		ptr_key[2] = dfs->root.oid.hi;
	}
	memcpy(key + DCACHE_KEY_PREF_SIZE, name, len);
	key_len = DCACHE_KEY_PREF_SIZE + len;
	rc      = shm_lru_get(dcache->d_shm_lru_dentry, key, key_len, &node_found, (void **)&value);
	D_DEBUG(DB_TRACE, "dentry cache %s: name=" DF_PATH "\n", (rc != 0) ? "miss" : "hit",
		DP_PATH(name));
	if (rc == ENOENT) {
		/* record is not found */
		rc = dcache_add(dcache, parent, name, len, key, key_len, &rec, mode, stbuf);
		if (rc)
			D_GOTO(err, rc);
	} else {
		D_ALLOC_PTR(rec);
		if (rec == NULL)
			D_GOTO(err, rc = ENOMEM);

		rc = dfs_obj_deserialize(dfs, flags, value, rec);
		if (rc)
			D_GOTO(err, rc);

		/** handle following symlinks outside of the dcache */
		if (S_ISLNK(rec->mode) && !(flags & O_NOFOLLOW)) {
			dfs_obj_t *sym;

			rc = lookup_rel_path(dfs, parent, rec->value, flags, &sym, mode, stbuf, 0);
			if (rc)
				D_GOTO(err, rc);
			D_FREE(rec);
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
					D_GOTO(err, rc);
				memcpy(&rec->dc_stbuf, stbuf, sizeof(struct stat));
				rec->dc_stated = true;
				/* stat and dc_stated are updated, need to update the record in
				 * cache too. The data buffer size is stored as an integer at the
				 * very beginning of data.
				 */
				val_size = *((size_t *)value) & 0xFFFFFFFF;
				rc       = dfs_obj_serialize(rec, (uint8_t *)value, &val_size);
				if (rc != 0)
					D_GOTO(err, rc);
			}
		}
		if (mode)
			*mode = rec->mode;
		rec->dc = dfs->dcache;
		rc      = 0;
	}

done:
	D_ASSERT(rec != NULL);
	*_rec = rec;
	if (node_found)
		shm_lru_node_dec_ref(node_found);
	return rc;

err:
	if (node_found)
		shm_lru_node_dec_ref(node_found);
	D_FREE(rec);
	return rc;
}

// dcache_readdir(dfs_dcache_t *dcache, dfs_obj_t *obj, dfs_dir_anchor_t *anchor, struct dirent dir)
// {}
