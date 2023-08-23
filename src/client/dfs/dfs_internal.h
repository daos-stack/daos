/**
 * (C) Copyright 2019-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This is an extension of the DAOS File System API
 *
 * src/client/dfs/dfs_internal.h
 */
#ifndef __DFS_INTERNAL_H__
#define __DFS_INTERNAL_H__

#if defined(__cplusplus)
extern "C" {
#endif

#include <sys/stat.h>
#include <daos.h>
#include <daos_fs.h>

/** enum for hash entry type */
enum {
	DFS_H_POOL,
	DFS_H_CONT,
};

/** hash entry for open pool/container handles */
struct dfs_mnt_hdls {
	d_list_t	entry;
	char		value[DAOS_PROP_LABEL_MAX_LEN * 2 + 1];
	daos_handle_t	handle;
	int		ref;
	int		type;
};

struct dfs_mnt_hdls *
dfs_hdl_lookup(const char *str, int type, const char *pool);
void
dfs_hdl_release(struct dfs_mnt_hdls *hdl);
int
dfs_hdl_insert(const char *str, int type, const char *pool, daos_handle_t *oh,
	       struct dfs_mnt_hdls **_hdl);
int
dfs_hdl_cont_destroy(const char *pool, const char *cont, bool force);
bool
dfs_is_init();

/*
 * Get the DFS superblock D-Key and A-Keys
 *
 * \param[out] dkey DFS superblock D-Key
 * \param[out] iods DFS superblock A-keys
 * \param[out] akey_count number of superblock A-keys
 * \param[out] dfs_entry_key_size key size of the inode entry
 * \param[out] dfs_entry_size size of the dfs entry
 *
 * \return              0 on success, errno code on failure.
 */
int
dfs_get_sb_layout(daos_key_t *dkey, daos_iod_t *iods[], int *akey_count,
		int *dfs_entry_key_size, int *dfs_entry_size);

/*
 * Releases the memory allocated by the dfs_get_sb_layout() function.
 *
 * \param[in] iods DFS superblock A-keys
*/
void
dfs_free_sb_layout(daos_iod_t *iods[]);

/** as dfs_open() but takes a stbuf to be populated if O_CREATE is specified
 *
 * If O_CREATE is set then entry->uid and entry->gid are popouated with the desired user,
 * otherwise read them from the calling process.
 */
int
dfs_open_stat(dfs_t *dfs, dfs_obj_t *parent, const char *name, mode_t mode,
	      int flags, daos_oclass_id_t cid, daos_size_t chunk_size,
	      const char *value, dfs_obj_t **obj, struct stat *stbuf);

int
dfs_lookupx(dfs_t *dfs, dfs_obj_t *parent, const char *name, int flags,
	    dfs_obj_t **obj, mode_t *mode, struct stat *stbuf, int xnr,
	    char *xnames[], void *xvals[], daos_size_t *xsizes);

/* moid is moved oid, oid is clobbered file.
 * This isn't yet fully compatible with dfuse because we also want to pass in a flag for if the
 * destination exists.
 */
int
dfs_move_internal(dfs_t *dfs, unsigned int flags, dfs_obj_t *parent, const char *name,
		  dfs_obj_t *new_parent, const char *new_name, daos_obj_id_t *moid,
		  daos_obj_id_t *oid);

/* Set the in-memory parent, but takes the parent, rather than another file object */
void
dfs_update_parentfd(dfs_obj_t *obj, dfs_obj_t *new_parent, const char *name);

/** update chunk size and oclass of obj with the ones from new_obj */
void
dfs_obj_copy_attr(dfs_obj_t *dst_obj, dfs_obj_t *src_obj);

/*
 * Internal routine for the daos fs tool to update an existing chunk size of a file. Note that this
 * is just meant to be used for updating the chunk size on leaked oids for files that were relinked
 * in lost+found.
 */
int
dfs_file_update_chunk_size(dfs_t *dfs, dfs_obj_t *obj, daos_size_t csize);

/** Internal routine for the daos fs tool to fix a corrupted entry type in the mode bits */
int
dfs_obj_fix_type(dfs_t *dfs, dfs_obj_t *parent, const char *name);

/*
 * Internal routine to recreate a POSIX container if it was ever corrupted as part of a catastrophic
 * recovery event.
 */
int
dfs_recreate_sb(daos_handle_t coh, dfs_attr_t *attr);

/*
 * Internal routine to relink the root object into the SB in case the SB entry was removed as part
 * of a catastrophic recovery event.
 */
int
dfs_relink_root(daos_handle_t coh);

/** Internal routine for async ostat.*/
int
dfs_ostatx(dfs_t *dfs, dfs_obj_t *obj, struct stat *stbuf, daos_event_t *ev);

#if defined(__cplusplus)
}
#endif
#endif /* __DFS_INTERNAL_H__ */
