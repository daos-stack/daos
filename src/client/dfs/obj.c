/**
 * (C) Copyright 2018-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

/** DFS metadata ops that apply for files, dirs, and symlinks */

#define D_LOGFAC DD_FAC(dfs)

#include <daos/common.h>
#include <daos/container.h>
#include <daos/event.h>
#include <daos/object.h>

#include "dfs_internal.h"

static int
check_access(uid_t c_uid, gid_t c_gid, uid_t uid, gid_t gid, mode_t mode, int mask)
{
	mode_t base_mask;

	if (mode == 0)
		return EACCES;

	/** set base_mask to others at first step */
	base_mask = S_IRWXO;
	/** update base_mask if uid matches */
	if (uid == c_uid)
		base_mask |= S_IRWXU;
	/** update base_mask if gid matches */
	if (gid == c_gid)
		base_mask |= S_IRWXG;

	/** AND the object mode with the base_mask to determine access */
	mode &= base_mask;

	/** Execute check */
	if (X_OK == (mask & X_OK))
		if (0 == (mode & (S_IXUSR | S_IXGRP | S_IXOTH)))
			return EACCES;

	/** Write check */
	if (W_OK == (mask & W_OK))
		if (0 == (mode & (S_IWUSR | S_IWGRP | S_IWOTH)))
			return EACCES;

	/** Read check */
	if (R_OK == (mask & R_OK))
		if (0 == (mode & (S_IRUSR | S_IRGRP | S_IROTH)))
			return EACCES;

	/** TODO - check ACL, attributes (immutable, append) etc. */
	return 0;
}

void
dfs_obj_copy_attr(dfs_obj_t *obj, dfs_obj_t *src_obj)
{
	if (S_ISDIR(obj->mode)) {
		obj->d.oclass     = src_obj->d.oclass;
		obj->d.chunk_size = src_obj->d.chunk_size;
	}
}

int
dfs_obj_get_info(dfs_t *dfs, dfs_obj_t *obj, dfs_obj_info_t *info)
{
	int rc = 0;

	if (obj == NULL || info == NULL)
		return EINVAL;

	switch (obj->mode & S_IFMT) {
	case S_IFDIR:
		/** the oclass of the directory object itself */
		info->doi_oclass_id = daos_obj_id2class(obj->oid);

		/** what is the default oclass files and dirs will be created with in this dir */
		if (obj->d.oclass) {
			info->doi_dir_oclass_id  = obj->d.oclass;
			info->doi_file_oclass_id = obj->d.oclass;
		} else {
			if (dfs->attr.da_dir_oclass_id)
				info->doi_dir_oclass_id = dfs->attr.da_dir_oclass_id;
			else
				rc = daos_obj_get_oclass(dfs->coh, 0, 0, 0,
							 &info->doi_dir_oclass_id);

			if (dfs->attr.da_file_oclass_id)
				info->doi_file_oclass_id = dfs->attr.da_file_oclass_id;
			else
				rc = daos_obj_get_oclass(dfs->coh, 0, 0, 0,
							 &info->doi_file_oclass_id);

			if (rc) {
				D_ERROR("daos_obj_get_oclass() failed " DF_RC "\n", DP_RC(rc));
				return daos_der2errno(rc);
			}
		}

		if (obj->d.chunk_size)
			info->doi_chunk_size = obj->d.chunk_size;
		else if (dfs->attr.da_chunk_size)
			info->doi_chunk_size = dfs->attr.da_chunk_size;
		else
			info->doi_chunk_size = DFS_DEFAULT_CHUNK_SIZE;
		break;
	case S_IFREG: {
		daos_size_t cell_size;

		rc = daos_array_get_attr(obj->oh, &info->doi_chunk_size, &cell_size);
		if (rc)
			return daos_der2errno(rc);

		info->doi_oclass_id = daos_obj_id2class(obj->oid);
		break;
	}
	case S_IFLNK:
		info->doi_oclass_id  = 0;
		info->doi_chunk_size = 0;
		break;
	default:
		D_ERROR("Invalid entry type (not a dir, file, symlink).\n");
		return EINVAL;
	}

	return 0;
}

static int
open_file(dfs_t *dfs, dfs_obj_t *parent, int flags, daos_oclass_id_t cid, daos_size_t chunk_size,
	  struct dfs_entry *entry, daos_size_t *size, size_t len, dfs_obj_t *file)
{
	bool exists;
	int  daos_mode;
	bool oexcl  = flags & O_EXCL;
	bool ocreat = flags & O_CREAT;
	int  rc;

	if (ocreat) {
		struct timespec now;

		/*
		 * Create the entry with conditional insert. If we get EEXIST:
		 * - With O_EXCL operation fails.
		 * - Without O_EXCL we can just open the file.
		 */

		/** set oclass for file. order: API, parent dir, cont default */
		if (cid == 0) {
			if (parent->d.oclass == 0)
				cid = dfs->attr.da_file_oclass_id;
			else
				cid = parent->d.oclass;
		}

		/** same logic for chunk size */
		if (chunk_size == 0) {
			if (parent->d.chunk_size == 0)
				chunk_size = dfs->attr.da_chunk_size;
			else
				chunk_size = parent->d.chunk_size;
		}

		/** Get new OID for the file */
		rc = oid_gen(dfs, cid, true, &file->oid);
		if (rc != 0)
			return rc;
		oid_cp(&entry->oid, file->oid);

		/** Open the array object for the file */
		rc = daos_array_open_with_attr(dfs->coh, file->oid, DAOS_TX_NONE, DAOS_OO_RW, 1,
					       chunk_size, &file->oh, NULL);
		if (rc != 0) {
			D_ERROR("daos_array_open_with_attr() failed " DF_RC "\n", DP_RC(rc));
			return daos_der2errno(rc);
		}

		/** Create and insert entry in parent dir object. */
		entry->mode = file->mode;
		rc          = clock_gettime(CLOCK_REALTIME, &now);
		if (rc)
			return errno;
		entry->mtime = entry->ctime = now.tv_sec;
		entry->mtime_nano = entry->ctime_nano = now.tv_nsec;
		entry->chunk_size                     = chunk_size;

		rc = insert_entry(dfs->layout_v, parent->oh, DAOS_TX_NONE, file->name, len,
				  DAOS_COND_DKEY_INSERT, entry);
		if (rc == EEXIST && !oexcl) {
			int rc2;

			/** just try fetching entry to open the file */
			rc2 = daos_array_close(file->oh, NULL);
			if (rc2) {
				D_ERROR("daos_array_close() failed " DF_RC "\n", DP_RC(rc2));
				return daos_der2errno(rc2);
			}
		} else if (rc) {
			int rc2;

			rc2 = daos_array_close(file->oh, NULL);
			if (rc2)
				D_ERROR("daos_array_close() failed " DF_RC "\n", DP_RC(rc2));
			D_DEBUG(DB_TRACE, "Insert file entry %s failed (%d)\n", file->name, rc);
			return rc;
		} else {
			D_ASSERT(rc == 0);
			return 0;
		}
	}

	/* Check if parent has the filename entry */
	rc = fetch_entry(dfs->layout_v, parent->oh, DAOS_TX_NONE, file->name, len, false, &exists,
			 entry, 0, NULL, NULL, NULL);
	if (rc) {
		D_DEBUG(DB_TRACE, "fetch_entry %s failed %d.\n", file->name, rc);
		return rc;
	}

	if (!exists)
		return ENOENT;

	if (!S_ISREG(entry->mode)) {
		D_FREE(entry->value);
		return EINVAL;
	}

	daos_mode = get_daos_obj_mode(flags);
	if (daos_mode == -1)
		return EINVAL;

	D_ASSERT(entry->chunk_size);

	/** Open the byte array */
	file->mode = entry->mode;
	rc         = daos_array_open_with_attr(dfs->coh, entry->oid, DAOS_TX_NONE, daos_mode, 1,
					       entry->chunk_size, &file->oh, NULL);
	if (rc != 0) {
		D_ERROR("daos_array_open_with_attr() failed, " DF_RC "\n", DP_RC(rc));
		return daos_der2errno(rc);
	}

	if (flags & O_TRUNC) {
		rc = daos_array_set_size(file->oh, DAOS_TX_NONE, 0, NULL);
		if (rc) {
			D_ERROR("Failed to truncate file " DF_RC "\n", DP_RC(rc));
			daos_array_close(file->oh, NULL);
			return daos_der2errno(rc);
		}
		if (size)
			*size = 0;
	} else if (size) {
		rc = daos_array_get_size(file->oh, DAOS_TX_NONE, size, NULL);
		if (rc != 0) {
			D_ERROR("daos_array_get_size() failed (%d)\n", rc);
			daos_array_close(file->oh, NULL);
			return daos_der2errno(rc);
		}
	}
	oid_cp(&file->oid, entry->oid);
	return 0;
}

static int
open_symlink(dfs_t *dfs, dfs_obj_t *parent, int flags, daos_oclass_id_t cid, const char *value,
	     struct dfs_entry *entry, size_t len, dfs_obj_t *sym)
{
	size_t value_len;
	int    rc;

	if (flags & O_CREAT) {
		struct timespec now;

		if (value == NULL)
			return EINVAL;

		value_len = strnlen(value, DFS_MAX_PATH);

		if (value_len > DFS_MAX_PATH - 1)
			return EINVAL;

		/** set oclass. order: API, parent dir, cont default */
		if (cid == 0) {
			if (parent->d.oclass == 0)
				cid = dfs->attr.da_oclass_id;
			else
				cid = parent->d.oclass;
		}

		/*
		 * note that we don't use this object to store anything since
		 * the value is stored in the inode. This just an identifier for
		 * the symlink.
		 */
		rc = oid_gen(dfs, cid, false, &sym->oid);
		if (rc != 0)
			return rc;

		oid_cp(&entry->oid, sym->oid);
		entry->mode = sym->mode | S_IRWXO | S_IRWXU | S_IRWXG;
		rc          = clock_gettime(CLOCK_REALTIME, &now);
		if (rc)
			return errno;
		entry->mtime = entry->ctime = now.tv_sec;
		entry->mtime_nano = entry->ctime_nano = now.tv_nsec;
		D_STRNDUP(sym->value, value, value_len + 1);
		if (sym->value == NULL)
			return ENOMEM;
		entry->value     = sym->value;
		entry->value_len = value_len;

		rc = insert_entry(dfs->layout_v, parent->oh, DAOS_TX_NONE, sym->name, len,
				  DAOS_COND_DKEY_INSERT, entry);
		if (rc == EEXIST) {
			D_FREE(sym->value);
		} else if (rc != 0) {
			D_FREE(sym->value);
			D_ERROR("Inserting entry '%s' failed: %d (%s)\n", sym->name, rc,
				strerror(rc));
		}
		return rc;
	}

	return ENOTSUP;
}

static int
open_stat(dfs_t *dfs, dfs_obj_t *parent, const char *name, mode_t mode, int flags,
	  daos_oclass_id_t cid, daos_size_t chunk_size, const char *value, dfs_obj_t **_obj,
	  struct stat *stbuf)
{
	struct dfs_entry entry = {0};
	dfs_obj_t       *obj;
	size_t           len;
	daos_size_t      file_size = 0;
	int              rc;

	if (dfs == NULL || !dfs->mounted)
		return EINVAL;
	if (flags & O_APPEND)
		return ENOTSUP;
	if ((dfs->amode != O_RDWR) && (flags & O_CREAT))
		return EPERM;
	if (_obj == NULL)
		return EINVAL;
	if (S_ISLNK(mode) && value == NULL)
		return EINVAL;
	if (parent == NULL)
		parent = &dfs->root;
	else if (!S_ISDIR(parent->mode))
		return ENOTDIR;

	if (stbuf && !(flags & O_CREAT))
		return ENOTSUP;

	rc = check_name(name, &len);
	if (rc)
		return rc;

	D_ALLOC_PTR(obj);
	if (obj == NULL)
		return ENOMEM;

	if (flags & O_CREAT) {
		if (stbuf) {
			entry.uid = stbuf->st_uid;
			entry.gid = stbuf->st_gid;
		} else {
			entry.uid = geteuid();
			entry.gid = getegid();
		}
	}

	strncpy(obj->name, name, len + 1);
	obj->mode  = mode;
	obj->flags = flags;
	oid_cp(&obj->parent_oid, parent->oid);

	switch (mode & S_IFMT) {
	case S_IFREG:
		rc = open_file(dfs, parent, flags, cid, chunk_size, &entry,
			       stbuf ? &file_size : NULL, len, obj);
		if (rc) {
			D_DEBUG(DB_TRACE, "Failed to open file (%d)\n", rc);
			D_GOTO(out, rc);
		}
		break;
	case S_IFDIR:
		rc = open_dir(dfs, parent, flags, cid, &entry, len, obj);
		if (rc) {
			D_DEBUG(DB_TRACE, "Failed to open dir (%d)\n", rc);
			D_GOTO(out, rc);
		}
		file_size = sizeof(entry);
		break;
	case S_IFLNK:
		rc = open_symlink(dfs, parent, flags, cid, value, &entry, len, obj);
		if (rc) {
			D_DEBUG(DB_TRACE, "Failed to open symlink (%d)\n", rc);
			D_GOTO(out, rc);
		}
		file_size = entry.value_len;
		break;
	default:
		D_ERROR("Invalid entry type (not a dir, file, symlink).\n");
		D_GOTO(out, rc = EINVAL);
	}

out:
	if (rc == 0) {
		if (stbuf) {
			stbuf->st_size         = file_size;
			stbuf->st_nlink        = 1;
			stbuf->st_mode         = entry.mode;
			stbuf->st_uid          = entry.uid;
			stbuf->st_gid          = entry.gid;
			stbuf->st_mtim.tv_sec  = entry.mtime;
			stbuf->st_mtim.tv_nsec = entry.mtime_nano;
			stbuf->st_ctim.tv_sec  = entry.ctime;
			stbuf->st_ctim.tv_nsec = entry.ctime_nano;
			if (tspec_gt(stbuf->st_ctim, stbuf->st_mtim)) {
				stbuf->st_atim.tv_sec  = entry.ctime;
				stbuf->st_atim.tv_nsec = entry.ctime_nano;
			} else {
				stbuf->st_atim.tv_sec  = entry.mtime;
				stbuf->st_atim.tv_nsec = entry.mtime_nano;
			}
		}
		*_obj = obj;
	} else {
		D_FREE(obj);
	}

	return rc;
}

int
dfs_open(dfs_t *dfs, dfs_obj_t *parent, const char *name, mode_t mode, int flags,
	 daos_oclass_id_t cid, daos_size_t chunk_size, const char *value, dfs_obj_t **_obj)
{
	return open_stat(dfs, parent, name, mode, flags, cid, chunk_size, value, _obj, NULL);
}

int
dfs_open_stat(dfs_t *dfs, dfs_obj_t *parent, const char *name, mode_t mode, int flags,
	      daos_oclass_id_t cid, daos_size_t chunk_size, const char *value, dfs_obj_t **_obj,
	      struct stat *stbuf)
{
	return open_stat(dfs, parent, name, mode, flags, cid, chunk_size, value, _obj, stbuf);
}

int
dfs_dup(dfs_t *dfs, dfs_obj_t *obj, int flags, dfs_obj_t **_new_obj)
{
	dfs_obj_t   *new_obj;
	unsigned int daos_mode;
	int          rc;

	if (dfs == NULL || !dfs->mounted)
		return EINVAL;
	if (obj == NULL || _new_obj == NULL)
		return EINVAL;
	if (flags & O_APPEND)
		return ENOTSUP;

	daos_mode = get_daos_obj_mode(flags);
	if (daos_mode == -1)
		return EINVAL;

	D_ALLOC_PTR(new_obj);
	if (new_obj == NULL)
		return ENOMEM;

	switch (obj->mode & S_IFMT) {
	case S_IFDIR:
		rc = daos_obj_open(dfs->coh, obj->oid, daos_mode, &new_obj->oh, NULL);
		if (rc)
			D_GOTO(err, rc = daos_der2errno(rc));
		break;
	case S_IFREG: {
		char    buf[1024];
		d_iov_t ghdl;

		d_iov_set(&ghdl, buf, 1024);

		rc = daos_array_local2global(obj->oh, &ghdl);
		if (rc)
			D_GOTO(err, rc = daos_der2errno(rc));

		rc = daos_array_global2local(dfs->coh, ghdl, daos_mode, &new_obj->oh);
		if (rc)
			D_GOTO(err, rc = daos_der2errno(rc));
		break;
	}
	case S_IFLNK:
		D_STRNDUP(new_obj->value, obj->value, DFS_MAX_PATH - 1);
		if (new_obj->value == NULL)
			D_GOTO(err, rc = ENOMEM);
		break;
	default:
		D_ERROR("Invalid object type (not a dir, file, symlink).\n");
		D_GOTO(err, rc = EINVAL);
	}

	strncpy(new_obj->name, obj->name, DFS_MAX_NAME + 1);
	new_obj->mode  = obj->mode;
	new_obj->flags = flags;
	oid_cp(&new_obj->parent_oid, obj->parent_oid);
	oid_cp(&new_obj->oid, obj->oid);

	*_new_obj = new_obj;
	return 0;

err:
	D_FREE(new_obj);
	return rc;
}

/* Structure of global buffer for dfs_obj */
struct dfs_obj_glob {
	uint32_t      magic;
	uint32_t      flags;
	mode_t        mode;
	daos_obj_id_t oid;
	daos_obj_id_t parent_oid;
	daos_size_t   chunk_size;
	uuid_t        cont_uuid;
	uuid_t        coh_uuid;
	char          name[DFS_MAX_NAME + 1];
};

static inline daos_size_t
dfs_obj_glob_buf_size()
{
	return sizeof(struct dfs_obj_glob);
}

static inline void
swap_obj_glob(struct dfs_obj_glob *array_glob)
{
	D_ASSERT(array_glob != NULL);

	D_SWAP32S(&array_glob->magic);
	D_SWAP32S(&array_glob->mode);
	D_SWAP32S(&array_glob->flags);
	D_SWAP64S(&array_glob->oid.hi);
	D_SWAP64S(&array_glob->oid.lo);
	D_SWAP64S(&array_glob->parent_oid.hi);
	D_SWAP64S(&array_glob->parent_oid.lo);
	D_SWAP64S(&array_glob->chunk_size);
	/* skip cont_uuid */
	/* skip coh_uuid */
}

int
dfs_obj_local2global(dfs_t *dfs, dfs_obj_t *obj, d_iov_t *glob)
{
	struct dfs_obj_glob *obj_glob;
	uuid_t               coh_uuid;
	uuid_t               cont_uuid;
	daos_size_t          glob_buf_size;
	int                  rc = 0;

	if (obj == NULL || !S_ISREG(obj->mode))
		return EINVAL;

	if (glob == NULL) {
		D_ERROR("Invalid parameter, NULL glob pointer.\n");
		return EINVAL;
	}

	if (glob->iov_buf != NULL &&
	    (glob->iov_buf_len == 0 || glob->iov_buf_len < glob->iov_len)) {
		D_ERROR("Invalid parameter of glob, iov_buf %p, iov_buf_len "
			"" DF_U64 ", iov_len " DF_U64 ".\n",
			glob->iov_buf, glob->iov_buf_len, glob->iov_len);
		return EINVAL;
	}

	glob_buf_size = dfs_obj_glob_buf_size();

	if (glob->iov_buf == NULL) {
		glob->iov_buf_len = glob_buf_size;
		return 0;
	}

	rc = dc_cont_hdl2uuid(dfs->coh, &coh_uuid, &cont_uuid);
	if (rc != 0)
		return daos_der2errno(rc);

	if (glob->iov_buf_len < glob_buf_size) {
		D_DEBUG(DB_ANY,
			"Larger glob buffer needed (" DF_U64 " bytes "
			"provided, " DF_U64 " required).\n",
			glob->iov_buf_len, glob_buf_size);
		glob->iov_buf_len = glob_buf_size;
		return ENOBUFS;
	}
	glob->iov_len = glob_buf_size;

	/* init global handle */
	obj_glob        = (struct dfs_obj_glob *)glob->iov_buf;
	obj_glob->magic = DFS_OBJ_GLOB_MAGIC;
	obj_glob->mode  = obj->mode;
	obj_glob->flags = obj->flags;
	oid_cp(&obj_glob->oid, obj->oid);
	oid_cp(&obj_glob->parent_oid, obj->parent_oid);
	uuid_copy(obj_glob->coh_uuid, coh_uuid);
	uuid_copy(obj_glob->cont_uuid, cont_uuid);
	strncpy(obj_glob->name, obj->name, DFS_MAX_NAME + 1);
	obj_glob->name[DFS_MAX_NAME] = 0;
	rc                           = dfs_get_chunk_size(obj, &obj_glob->chunk_size);
	if (rc)
		return rc;

	return rc;
}

int
dfs_obj_global2local(dfs_t *dfs, int flags, d_iov_t glob, dfs_obj_t **_obj)
{
	struct dfs_obj_glob *obj_glob;
	dfs_obj_t           *obj;
	uuid_t               coh_uuid;
	uuid_t               cont_uuid;
	int                  daos_mode;
	int                  rc = 0;

	if (dfs == NULL || !dfs->mounted || _obj == NULL)
		return EINVAL;

	if (glob.iov_buf == NULL || glob.iov_buf_len < glob.iov_len ||
	    glob.iov_len != dfs_obj_glob_buf_size()) {
		D_ERROR("Invalid parameter of glob, iov_buf %p, "
			"iov_buf_len " DF_U64 ", iov_len " DF_U64 ".\n",
			glob.iov_buf, glob.iov_buf_len, glob.iov_len);
		return EINVAL;
	}

	obj_glob = (struct dfs_obj_glob *)glob.iov_buf;
	if (obj_glob->magic == D_SWAP32(DFS_OBJ_GLOB_MAGIC)) {
		swap_obj_glob(obj_glob);
		D_ASSERT(obj_glob->magic == DFS_OBJ_GLOB_MAGIC);
	} else if (obj_glob->magic != DFS_OBJ_GLOB_MAGIC) {
		D_ERROR("Bad magic value: %#x.\n", obj_glob->magic);
		return EINVAL;
	}

	/** Check container uuid mismatch */
	rc = dc_cont_hdl2uuid(dfs->coh, &coh_uuid, &cont_uuid);
	if (rc != 0)
		D_GOTO(out, rc = daos_der2errno(rc));
	if (uuid_compare(cont_uuid, obj_glob->cont_uuid) != 0) {
		D_ERROR("Container uuid mismatch, in coh: " DF_UUID ", "
			"in obj_glob:" DF_UUID "\n",
			DP_UUID(cont_uuid), DP_UUID(obj_glob->cont_uuid));
		return EINVAL;
	}

	D_ALLOC_PTR(obj);
	if (obj == NULL)
		return ENOMEM;

	oid_cp(&obj->oid, obj_glob->oid);
	oid_cp(&obj->parent_oid, obj_glob->parent_oid);
	strncpy(obj->name, obj_glob->name, DFS_MAX_NAME + 1);
	obj->name[DFS_MAX_NAME] = '\0';
	obj->mode               = obj_glob->mode;
	obj->flags              = flags ? flags : obj_glob->flags;

	daos_mode = get_daos_obj_mode(obj->flags);
	rc        = daos_array_open_with_attr(dfs->coh, obj->oid, DAOS_TX_NONE, daos_mode, 1,
					      obj_glob->chunk_size, &obj->oh, NULL);
	if (rc) {
		D_ERROR("daos_array_open_with_attr() failed, " DF_RC "\n", DP_RC(rc));
		D_FREE(obj);
		return daos_der2errno(rc);
	}

	*_obj = obj;
out:
	return rc;
}

int
dfs_release(dfs_obj_t *obj)
{
	int rc = 0;

	if (obj == NULL)
		return EINVAL;

	switch (obj->mode & S_IFMT) {
	case S_IFDIR:
		rc = daos_obj_close(obj->oh, NULL);
		break;
	case S_IFREG:
		rc = daos_array_close(obj->oh, NULL);
		break;
	case S_IFLNK:
		D_FREE(obj->value);
		break;
	default:
		D_ERROR("Invalid entry type (not a dir, file, symlink).\n");
		rc = -DER_IO_INVAL;
	}

	if (rc)
		D_ERROR("Failed to close DFS object, " DF_RC "\n", DP_RC(rc));
	else
		D_FREE(obj);
	return daos_der2errno(rc);
}

int
dfs_update_parent(dfs_obj_t *obj, dfs_obj_t *src_obj, const char *name)
{
	if (obj == NULL)
		return EINVAL;

	oid_cp(&obj->parent_oid, src_obj->parent_oid);
	if (name) {
		strncpy(obj->name, name, DFS_MAX_NAME);
		obj->name[DFS_MAX_NAME] = '\0';
	}

	return 0;
}

/* Update a in-memory object to a new parent, taking the parent directly */
void
dfs_update_parentfd(dfs_obj_t *obj, dfs_obj_t *new_parent, const char *name)
{
	oid_cp(&obj->parent_oid, new_parent->oid);

	D_ASSERT(name);
	strncpy(obj->name, name, DFS_MAX_NAME);
	obj->name[DFS_MAX_NAME] = '\0';
}

int
dfs_stat(dfs_t *dfs, dfs_obj_t *parent, const char *name, struct stat *stbuf)
{
	daos_handle_t oh;
	size_t        len;
	int           rc;

	if (dfs == NULL || !dfs->mounted)
		return EINVAL;
	if (parent == NULL)
		parent = &dfs->root;
	else if (!S_ISDIR(parent->mode))
		return ENOTDIR;

	if (name == NULL) {
		if (strcmp(parent->name, "/") != 0) {
			D_ERROR("Invalid path %s and entry name is NULL)\n", parent->name);
			return EINVAL;
		}
		name = parent->name;
		len  = strlen(parent->name);
		oh   = dfs->super_oh;
	} else {
		rc = check_name(name, &len);
		if (rc)
			return rc;
		oh = parent->oh;
	}

	return entry_stat(dfs, DAOS_TX_NONE, oh, name, len, NULL, true, stbuf, NULL);
}

int
dfs_ostat(dfs_t *dfs, dfs_obj_t *obj, struct stat *stbuf)
{
	daos_handle_t oh;
	int           rc;

	if (dfs == NULL || !dfs->mounted)
		return EINVAL;
	if (obj == NULL)
		return EINVAL;

	/** Open parent object and fetch entry of obj from it */
	rc = daos_obj_open(dfs->coh, obj->parent_oid, DAOS_OO_RO, &oh, NULL);
	if (rc)
		return daos_der2errno(rc);

	rc =
	    entry_stat(dfs, DAOS_TX_NONE, oh, obj->name, strlen(obj->name), obj, true, stbuf, NULL);
	if (rc)
		D_GOTO(out, rc);

out:
	daos_obj_close(oh, NULL);
	return rc;
}

struct dfs_statx_args {
	dfs_t        *dfs;
	dfs_obj_t    *obj;
	struct stat  *stbuf;
	daos_handle_t parent_oh;
};

struct statx_op_args {
	daos_key_t         dkey;
	daos_iod_t         iod;
	daos_recx_t        recx;
	d_sg_list_t        sgl;
	d_iov_t            sg_iovs[INODE_AKEYS];
	struct dfs_entry   entry;
	daos_array_stbuf_t array_stbuf;
};

int
ostatx_cb(tse_task_t *task, void *data)
{
	struct dfs_statx_args *args    = daos_task_get_args(task);
	struct statx_op_args  *op_args = *((struct statx_op_args **)data);
	int                    rc2, rc = task->dt_result;

	if (rc != 0) {
		D_CDEBUG(rc == -DER_NONEXIST, DLOG_DBG, DLOG_ERR,
			 "Failed to stat file: " DF_RC "\n", DP_RC(rc));
		task->dt_result = rc;
		D_GOTO(out, rc = task->dt_result);
	}

	if (args->obj->oid.hi != op_args->entry.oid.hi ||
	    args->obj->oid.lo != op_args->entry.oid.lo)
		D_GOTO(out, rc = -DER_ENOENT);

	rc = update_stbuf_times(op_args->entry, op_args->array_stbuf.st_max_epoch, args->stbuf,
				NULL);
	if (rc)
		D_GOTO(out, rc = daos_errno2der(rc));

	if (S_ISREG(args->obj->mode)) {
		args->stbuf->st_size   = op_args->array_stbuf.st_size;
		args->stbuf->st_blocks = (args->stbuf->st_size + (1 << 9) - 1) >> 9;
	} else if (S_ISDIR(args->obj->mode)) {
		args->stbuf->st_size = sizeof(op_args->entry);
	} else if (S_ISLNK(args->obj->mode)) {
		args->stbuf->st_size = op_args->entry.value_len;
	}

	args->stbuf->st_nlink = 1;
	args->stbuf->st_mode  = op_args->entry.mode;
	args->stbuf->st_uid   = op_args->entry.uid;
	args->stbuf->st_gid   = op_args->entry.gid;
	if (tspec_gt(args->stbuf->st_ctim, args->stbuf->st_mtim)) {
		args->stbuf->st_atim.tv_sec  = args->stbuf->st_ctim.tv_sec;
		args->stbuf->st_atim.tv_nsec = args->stbuf->st_ctim.tv_nsec;
	} else {
		args->stbuf->st_atim.tv_sec  = args->stbuf->st_mtim.tv_sec;
		args->stbuf->st_atim.tv_nsec = args->stbuf->st_mtim.tv_nsec;
	}

out:
	D_FREE(op_args);
	rc2 = daos_obj_close(args->parent_oh, NULL);
	if (rc == 0)
		rc = rc2;
	return rc;
}

int
statx_task(tse_task_t *task)
{
	struct dfs_statx_args *args = daos_task_get_args(task);
	struct statx_op_args  *op_args;
	tse_task_t            *fetch_task;
	daos_obj_fetch_t      *fetch_arg;
	tse_task_t            *stat_task;
	tse_sched_t           *sched     = tse_task2sched(task);
	bool                   need_stat = false;
	int                    i;
	int                    rc;

	D_ALLOC_PTR(op_args);
	if (op_args == NULL) {
		daos_obj_close(args->parent_oh, NULL);
		return -DER_NOMEM;
	}

	/** Create task to fetch entry. */
	rc = daos_task_create(DAOS_OPC_OBJ_FETCH, sched, 0, NULL, &fetch_task);
	if (rc != 0) {
		D_ERROR("daos_task_create() failed: " DF_RC "\n", DP_RC(rc));
		D_GOTO(err1_out, rc);
	}

	/** set obj_fetch parameters */
	d_iov_set(&op_args->dkey, (void *)args->obj->name, strlen(args->obj->name));
	d_iov_set(&op_args->iod.iod_name, INODE_AKEY_NAME, sizeof(INODE_AKEY_NAME) - 1);
	op_args->iod.iod_nr    = 1;
	op_args->recx.rx_idx   = 0;
	op_args->recx.rx_nr    = END_IDX;
	op_args->iod.iod_recxs = &op_args->recx;
	op_args->iod.iod_type  = DAOS_IOD_ARRAY;
	op_args->iod.iod_size  = 1;
	i                      = 0;
	d_iov_set(&op_args->sg_iovs[i++], &op_args->entry.mode, sizeof(mode_t));
	d_iov_set(&op_args->sg_iovs[i++], &op_args->entry.oid, sizeof(daos_obj_id_t));
	d_iov_set(&op_args->sg_iovs[i++], &op_args->entry.mtime, sizeof(uint64_t));
	d_iov_set(&op_args->sg_iovs[i++], &op_args->entry.ctime, sizeof(uint64_t));
	d_iov_set(&op_args->sg_iovs[i++], &op_args->entry.chunk_size, sizeof(daos_size_t));
	d_iov_set(&op_args->sg_iovs[i++], &op_args->entry.oclass, sizeof(daos_oclass_id_t));
	d_iov_set(&op_args->sg_iovs[i++], &op_args->entry.mtime_nano, sizeof(uint64_t));
	d_iov_set(&op_args->sg_iovs[i++], &op_args->entry.ctime_nano, sizeof(uint64_t));
	d_iov_set(&op_args->sg_iovs[i++], &op_args->entry.uid, sizeof(uid_t));
	d_iov_set(&op_args->sg_iovs[i++], &op_args->entry.gid, sizeof(gid_t));
	d_iov_set(&op_args->sg_iovs[i++], &op_args->entry.value_len, sizeof(daos_size_t));
	d_iov_set(&op_args->sg_iovs[i++], &op_args->entry.obj_hlc, sizeof(uint64_t));
	op_args->sgl.sg_nr     = i;
	op_args->sgl.sg_nr_out = 0;
	op_args->sgl.sg_iovs   = op_args->sg_iovs;

	fetch_arg        = daos_task_get_args(fetch_task);
	fetch_arg->oh    = args->parent_oh;
	fetch_arg->th    = DAOS_TX_NONE;
	fetch_arg->flags = DAOS_COND_DKEY_FETCH;
	fetch_arg->dkey  = &op_args->dkey;
	fetch_arg->nr    = 1;
	fetch_arg->iods  = &op_args->iod;
	fetch_arg->sgls  = &op_args->sgl;

	if (S_ISREG(args->obj->mode)) {
		daos_array_stat_t *stat_arg;

		rc = daos_task_create(DAOS_OPC_ARRAY_STAT, sched, 0, NULL, &stat_task);
		if (rc != 0) {
			D_ERROR("daos_task_create() failed: " DF_RC "\n", DP_RC(rc));
			D_GOTO(err2_out, rc);
		}

		/** set array_stat parameters */
		stat_arg        = daos_task_get_args(stat_task);
		stat_arg->oh    = args->obj->oh;
		stat_arg->th    = DAOS_TX_NONE;
		stat_arg->stbuf = &op_args->array_stbuf;
		need_stat       = true;
	} else if (S_ISDIR(args->obj->mode)) {
		daos_obj_query_key_t *stat_arg;

		rc = daos_task_create(DAOS_OPC_OBJ_QUERY_KEY, sched, 0, NULL, &stat_task);
		if (rc != 0) {
			D_ERROR("daos_task_create() failed: " DF_RC "\n", DP_RC(rc));
			D_GOTO(err2_out, rc);
		}

		/** set obj_query parameters */
		stat_arg            = daos_task_get_args(stat_task);
		stat_arg->oh        = args->obj->oh;
		stat_arg->th        = DAOS_TX_NONE;
		stat_arg->max_epoch = &op_args->array_stbuf.st_max_epoch;
		stat_arg->flags     = 0;
		stat_arg->dkey      = NULL;
		stat_arg->akey      = NULL;
		stat_arg->recx      = NULL;
		need_stat           = true;
	}

	rc = tse_task_register_deps(task, 1, &fetch_task);
	if (rc) {
		D_ERROR("tse_task_register_deps() failed: " DF_RC "\n", DP_RC(rc));
		D_GOTO(err3_out, rc);
	}
	if (need_stat) {
		rc = tse_task_register_deps(task, 1, &stat_task);
		if (rc) {
			D_ERROR("tse_task_register_deps() failed: " DF_RC "\n", DP_RC(rc));
			tse_task_complete(stat_task, rc);
			D_GOTO(err1_out, rc);
		}
	}
	rc = tse_task_register_comp_cb(task, ostatx_cb, &op_args, sizeof(op_args));
	if (rc != 0) {
		D_ERROR("tse_task_register_comp_cb() failed: " DF_RC "\n", DP_RC(rc));
		D_GOTO(err1_out, rc);
	}

	tse_task_schedule(fetch_task, true);
	if (need_stat)
		tse_task_schedule(stat_task, true);
	return rc;
err3_out:
	if (need_stat)
		tse_task_complete(stat_task, rc);
err2_out:
	tse_task_complete(fetch_task, rc);
err1_out:
	D_FREE(op_args);
	daos_obj_close(args->parent_oh, NULL);
	return rc;
}

int
dfs_ostatx(dfs_t *dfs, dfs_obj_t *obj, struct stat *stbuf, daos_event_t *ev)
{
	daos_handle_t          oh;
	tse_task_t            *task;
	struct dfs_statx_args *args;
	int                    rc;

	if (dfs == NULL || !dfs->mounted)
		return EINVAL;
	if (obj == NULL)
		return EINVAL;
	if (ev == NULL)
		return dfs_ostat(dfs, obj, stbuf);

	rc = daos_obj_open(dfs->coh, obj->parent_oid, DAOS_OO_RO, &oh, NULL);
	if (rc)
		return daos_der2errno(rc);

	rc = dc_task_create(statx_task, NULL, ev, &task);
	if (rc) {
		daos_obj_close(oh, NULL);
		return daos_der2errno(rc);
	}
	D_ASSERT(ev);
	daos_event_errno_rc(ev);

	args            = dc_task_get_args(task);
	args->dfs       = dfs;
	args->obj       = obj;
	args->parent_oh = oh;
	args->stbuf     = stbuf;

	/** The parent oh is closed in the body function of the task even if an error occurred. */
	rc = dc_task_schedule(task, true);
	return daos_der2errno(rc);
}

int
dfs_access(dfs_t *dfs, dfs_obj_t *parent, const char *name, int mask)
{
	daos_handle_t    oh;
	bool             exists;
	struct dfs_entry entry = {0};
	size_t           len;
	dfs_obj_t       *sym;
	int              rc;

	if (dfs == NULL || !dfs->mounted)
		return EINVAL;
	if (((mask & W_OK) == W_OK) && dfs->amode != O_RDWR)
		return EPERM;
	if (parent == NULL)
		parent = &dfs->root;
	else if (!S_ISDIR(parent->mode))
		return ENOTDIR;
	if (name == NULL) {
		if (strcmp(parent->name, "/") != 0) {
			D_ERROR("Invalid path %s and entry name is NULL\n", parent->name);
			return EINVAL;
		}
		name = parent->name;
		len  = strlen(name);
		oh   = dfs->super_oh;
	} else {
		rc = check_name(name, &len);
		if (rc)
			return rc;
		oh = parent->oh;
	}

	/* Check if parent has the entry */
	rc = fetch_entry(dfs->layout_v, oh, DAOS_TX_NONE, name, len, true, &exists, &entry, 0, NULL,
			 NULL, NULL);
	if (rc)
		return rc;

	if (!exists)
		return ENOENT;

	if (!S_ISLNK(entry.mode)) {
		if (mask == F_OK)
			return 0;

		/** Use real uid and gid for access() */
		return check_access(entry.uid, entry.gid, getuid(), getgid(), entry.mode, mask);
	}

	D_ASSERT(entry.value);

	rc = lookup_rel_path(dfs, parent, entry.value, O_RDONLY, &sym, NULL, NULL, 0);
	if (rc) {
		D_DEBUG(DB_TRACE, "Failed to lookup symlink %s\n", entry.value);
		D_GOTO(out, rc);
	}

	if (mask != F_OK)
		rc = check_access(entry.uid, entry.gid, getuid(), getgid(), sym->mode, mask);
	dfs_release(sym);
out:
	D_FREE(entry.value);
	return rc;
}

int
dfs_chmod(dfs_t *dfs, dfs_obj_t *parent, const char *name, mode_t mode)
{
	daos_handle_t    oh;
	daos_handle_t    th = DAOS_TX_NONE;
	bool             exists;
	struct dfs_entry entry = {0};
	d_sg_list_t      sgl;
	d_iov_t          sg_iovs[3];
	daos_iod_t       iod;
	daos_recx_t      recxs[3];
	daos_key_t       dkey;
	size_t           len;
	dfs_obj_t       *sym;
	mode_t           orig_mode;
	const char      *entry_name;
	struct timespec  now;
	int              rc;

	if (dfs == NULL || !dfs->mounted)
		return EINVAL;
	if (dfs->amode != O_RDWR)
		return EPERM;
	if (parent == NULL)
		parent = &dfs->root;
	else if (!S_ISDIR(parent->mode))
		return ENOTDIR;
	if (name == NULL) {
		if (strcmp(parent->name, "/") != 0) {
			D_ERROR("Invalid path %s and entry name is NULL)\n", parent->name);
			return EINVAL;
		}
		name = parent->name;
		len  = strlen(name);
		oh   = dfs->super_oh;
	} else {
		rc = check_name(name, &len);
		if (rc)
			return rc;
		oh = parent->oh;
	}

	/** sticky bit is not supported */
	if (mode & S_ISVTX) {
		D_ERROR("sticky bit is not supported.\n");
		return ENOTSUP;
	}

	/* Check if parent has the entry */
	rc = fetch_entry(dfs->layout_v, oh, DAOS_TX_NONE, name, len, true, &exists, &entry, 0, NULL,
			 NULL, NULL);
	if (rc)
		return rc;

	if (!exists)
		return ENOENT;

	/** resolve symlink */
	if (S_ISLNK(entry.mode)) {
		D_ASSERT(entry.value);

		rc = lookup_rel_path(dfs, parent, entry.value, O_RDWR, &sym, NULL, NULL, 0);
		if (rc) {
			D_ERROR("Failed to lookup symlink %s\n", entry.value);
			D_FREE(entry.value);
			return rc;
		}

		rc = daos_obj_open(dfs->coh, sym->parent_oid, DAOS_OO_RW, &oh, NULL);
		D_FREE(entry.value);
		if (rc) {
			dfs_release(sym);
			return daos_der2errno(rc);
		}

		orig_mode  = sym->mode;
		entry_name = sym->name;
		len        = strlen(entry_name);
	} else {
		orig_mode  = entry.mode;
		entry_name = name;
	}

	if ((mode & S_IFMT) && (orig_mode & S_IFMT) != (mode & S_IFMT)) {
		D_ERROR("Cannot change entry type\n");
		D_GOTO(out, rc = EINVAL);
	}

	/** set the type mode in case user has not passed it */
	mode |= orig_mode & S_IFMT;

	/** set dkey as the entry name */
	d_iov_set(&dkey, (void *)entry_name, len);
	d_iov_set(&iod.iod_name, INODE_AKEY_NAME, sizeof(INODE_AKEY_NAME) - 1);
	iod.iod_recxs   = recxs;
	iod.iod_type    = DAOS_IOD_ARRAY;
	iod.iod_size    = 1;
	iod.iod_nr      = 3;
	recxs[0].rx_idx = MODE_IDX;
	recxs[0].rx_nr  = sizeof(mode_t);
	recxs[1].rx_idx = CTIME_IDX;
	recxs[1].rx_nr  = sizeof(uint64_t);
	recxs[2].rx_idx = CTIME_NSEC_IDX;
	recxs[2].rx_nr  = sizeof(uint64_t);

	rc = clock_gettime(CLOCK_REALTIME, &now);
	if (rc)
		D_GOTO(out, rc = errno);

	/** set sgl for update */
	sgl.sg_nr     = 3;
	sgl.sg_nr_out = 0;
	sgl.sg_iovs   = &sg_iovs[0];
	d_iov_set(&sg_iovs[0], &mode, sizeof(mode_t));
	d_iov_set(&sg_iovs[1], &now.tv_sec, sizeof(uint64_t));
	d_iov_set(&sg_iovs[2], &now.tv_nsec, sizeof(uint64_t));

	rc = daos_obj_update(oh, th, DAOS_COND_DKEY_UPDATE, &dkey, 1, &iod, &sgl, NULL);
	if (rc) {
		D_ERROR("Failed to update mode, " DF_RC "\n", DP_RC(rc));
		D_GOTO(out, rc = daos_der2errno(rc));
	}

out:
	if (S_ISLNK(entry.mode)) {
		dfs_release(sym);
		daos_obj_close(oh, NULL);
	}
	return rc;
}

int
dfs_chown(dfs_t *dfs, dfs_obj_t *parent, const char *name, uid_t uid, gid_t gid, int flags)
{
	daos_handle_t    oh;
	daos_handle_t    th = DAOS_TX_NONE;
	bool             exists;
	struct dfs_entry entry = {0};
	daos_key_t       dkey;
	d_sg_list_t      sgl;
	d_iov_t          sg_iovs[4];
	daos_iod_t       iod;
	daos_recx_t      recxs[4];
	size_t           len;
	dfs_obj_t       *sym;
	const char      *entry_name;
	int              i;
	struct timespec  now;
	int              rc;

	if (dfs == NULL || !dfs->mounted)
		return EINVAL;
	if (dfs->amode != O_RDWR)
		return EPERM;
	if (parent == NULL)
		parent = &dfs->root;
	else if (!S_ISDIR(parent->mode))
		return ENOTDIR;
	if (name == NULL) {
		if (strcmp(parent->name, "/") != 0) {
			D_ERROR("Invalid path %s and entry name is NULL)\n", parent->name);
			return EINVAL;
		}
		name = parent->name;
		len  = strlen(name);
		oh   = dfs->super_oh;
	} else {
		rc = check_name(name, &len);
		if (rc)
			return rc;
		oh = parent->oh;
	}

	/* Check if parent has the entry */
	rc = fetch_entry(dfs->layout_v, oh, DAOS_TX_NONE, name, len, true, &exists, &entry, 0, NULL,
			 NULL, NULL);
	if (rc)
		return rc;

	if (!exists)
		return ENOENT;

	if (uid == -1 && gid == -1)
		D_GOTO(out, rc = 0);

	/** resolve symlink */
	if (!(flags & O_NOFOLLOW) && S_ISLNK(entry.mode)) {
		D_ASSERT(entry.value);
		rc = lookup_rel_path(dfs, parent, entry.value, O_RDWR, &sym, NULL, NULL, 0);
		if (rc) {
			D_DEBUG(DB_TRACE, "Failed to lookup symlink '%s': %d (%s)\n", entry.value,
				rc, strerror(rc));
			D_FREE(entry.value);
			return rc;
		}

		rc = daos_obj_open(dfs->coh, sym->parent_oid, DAOS_OO_RW, &oh, NULL);
		D_FREE(entry.value);
		if (rc) {
			dfs_release(sym);
			return daos_der2errno(rc);
		}
		entry_name = sym->name;
		len        = strlen(entry_name);
	} else {
		if (S_ISLNK(entry.mode))
			D_FREE(entry.value);
		entry_name = name;
	}

	rc = clock_gettime(CLOCK_REALTIME, &now);
	if (rc)
		D_GOTO(out, rc = errno);

	i               = 0;
	recxs[i].rx_idx = CTIME_IDX;
	recxs[i].rx_nr  = sizeof(uint64_t);
	d_iov_set(&sg_iovs[i], &now.tv_sec, sizeof(uint64_t));
	i++;

	recxs[i].rx_idx = CTIME_NSEC_IDX;
	recxs[i].rx_nr  = sizeof(uint64_t);
	d_iov_set(&sg_iovs[i], &now.tv_nsec, sizeof(uint64_t));
	i++;

	/** not enforcing any restriction on chown more than write access to the container */
	if (uid != -1) {
		d_iov_set(&sg_iovs[i], &uid, sizeof(uid_t));
		recxs[i].rx_idx = UID_IDX;
		recxs[i].rx_nr  = sizeof(uid_t);
		i++;
	}
	if (gid != -1) {
		d_iov_set(&sg_iovs[i], &gid, sizeof(gid_t));
		recxs[i].rx_idx = GID_IDX;
		recxs[i].rx_nr  = sizeof(gid_t);
		i++;
	}

	/** set dkey as the entry name */
	d_iov_set(&dkey, (void *)entry_name, len);
	d_iov_set(&iod.iod_name, INODE_AKEY_NAME, sizeof(INODE_AKEY_NAME) - 1);
	iod.iod_nr    = i;
	iod.iod_recxs = recxs;
	iod.iod_type  = DAOS_IOD_ARRAY;
	iod.iod_size  = 1;

	/** set sgl for update */
	sgl.sg_nr     = i;
	sgl.sg_nr_out = 0;
	sgl.sg_iovs   = &sg_iovs[0];

	rc = daos_obj_update(oh, th, DAOS_COND_DKEY_UPDATE, &dkey, 1, &iod, &sgl, NULL);
	if (rc) {
		D_ERROR("Failed to update owner/group, " DF_RC "\n", DP_RC(rc));
		D_GOTO(out, rc = daos_der2errno(rc));
	}

out:
	if (!(flags & O_NOFOLLOW) && S_ISLNK(entry.mode)) {
		dfs_release(sym);
		daos_obj_close(oh, NULL);
	}
	return rc;
}

int
dfs_osetattr(dfs_t *dfs, dfs_obj_t *obj, struct stat *stbuf, int flags)
{
	daos_handle_t      th = DAOS_TX_NONE;
	daos_key_t         dkey;
	daos_handle_t      oh;
	d_sg_list_t        sgl;
	d_iov_t            sg_iovs[10];
	daos_iod_t         iod;
	daos_recx_t        recxs[10];
	bool               set_size  = false;
	bool               set_mtime = false;
	bool               set_ctime = false;
	int                i = 0, hlc_recx_idx = 0;
	size_t             len;
	uint64_t           obj_hlc     = 0;
	struct stat        rstat       = {};
	daos_array_stbuf_t array_stbuf = {0};
	int                rc;

	if (dfs == NULL || !dfs->mounted)
		return EINVAL;
	if (obj == NULL)
		return EINVAL;
	if (dfs->amode != O_RDWR)
		return EPERM;
	if ((obj->flags & O_ACCMODE) == O_RDONLY)
		return EPERM;
	if (flags & DFS_SET_ATTR_MODE) {
		if ((stbuf->st_mode & S_IFMT) != (obj->mode & S_IFMT))
			return EINVAL;

		/** sticky bit is not supported */
		if (mode & S_ISVTX) {
			D_DEBUG(DB_TRACE, "sticky bit is not supported.\n");
			return ENOTSUP;
		}
	}

	/** Open parent object and fetch entry of obj from it */
	rc = daos_obj_open(dfs->coh, obj->parent_oid, DAOS_OO_RW, &oh, NULL);
	if (rc)
		return daos_der2errno(rc);

	len = strlen(obj->name);

	/*
	 * Fetch the remote entry first so we can check the oid, then keep track locally of what has
	 * been updated. If we are setting the file size, there is no need to query it.
	 */
	if (flags & DFS_SET_ATTR_SIZE)
		rc = entry_stat(dfs, th, oh, obj->name, len, obj, false, &rstat, &obj_hlc);
	else
		rc = entry_stat(dfs, th, oh, obj->name, len, obj, true, &rstat, &obj_hlc);
	if (rc)
		D_GOTO(out_obj, rc);

	/** set dkey as the entry name */
	d_iov_set(&dkey, (void *)obj->name, len);
	d_iov_set(&iod.iod_name, INODE_AKEY_NAME, sizeof(INODE_AKEY_NAME) - 1);
	iod.iod_recxs = recxs;
	iod.iod_type  = DAOS_IOD_ARRAY;
	iod.iod_size  = 1;

	/** need to update ctime */
	if (flags & DFS_SET_ATTR_MODE || flags & DFS_SET_ATTR_MTIME || flags & DFS_SET_ATTR_UID ||
	    flags & DFS_SET_ATTR_GID) {
		struct timespec now;

		rc = clock_gettime(CLOCK_REALTIME, &now);
		if (rc)
			D_GOTO(out_obj, rc = errno);
		rstat.st_ctim.tv_sec  = now.tv_sec;
		rstat.st_ctim.tv_nsec = now.tv_nsec;
		set_ctime             = true;

		d_iov_set(&sg_iovs[i], &rstat.st_ctim.tv_sec, sizeof(uint64_t));
		recxs[i].rx_idx = CTIME_IDX;
		recxs[i].rx_nr  = sizeof(uint64_t);
		i++;

		d_iov_set(&sg_iovs[i], &rstat.st_ctim.tv_nsec, sizeof(uint64_t));
		recxs[i].rx_idx = CTIME_NSEC_IDX;
		recxs[i].rx_nr  = sizeof(uint64_t);
		i++;
	}

	if (flags & DFS_SET_ATTR_MODE) {
		d_iov_set(&sg_iovs[i], &stbuf->st_mode, sizeof(mode_t));
		recxs[i].rx_idx = MODE_IDX;
		recxs[i].rx_nr  = sizeof(mode_t);
		i++;

		flags &= ~DFS_SET_ATTR_MODE;
		rstat.st_mode = stbuf->st_mode;
	}
	if (flags & DFS_SET_ATTR_ATIME) {
		flags &= ~DFS_SET_ATTR_ATIME;
		D_WARN("ATIME is no longer stored in DFS and setting it is ignored.\n");
	}
	if (flags & DFS_SET_ATTR_MTIME) {
		d_iov_set(&sg_iovs[i], &stbuf->st_mtim.tv_sec, sizeof(uint64_t));
		recxs[i].rx_idx = MTIME_IDX;
		recxs[i].rx_nr  = sizeof(uint64_t);
		i++;

		d_iov_set(&sg_iovs[i], &stbuf->st_mtim.tv_nsec, sizeof(uint64_t));
		recxs[i].rx_idx = MTIME_NSEC_IDX;
		recxs[i].rx_nr  = sizeof(uint64_t);
		i++;

		d_iov_set(&sg_iovs[i], &obj_hlc, sizeof(uint64_t));
		recxs[i].rx_idx = HLC_IDX;
		recxs[i].rx_nr  = sizeof(uint64_t);
		if (flags & DFS_SET_ATTR_SIZE) {
			/** we need to update this again after the set size */
			hlc_recx_idx = i;
		}
		i++;

		set_mtime = true;
		flags &= ~DFS_SET_ATTR_MTIME;
		rstat.st_mtim.tv_sec  = stbuf->st_mtim.tv_sec;
		rstat.st_mtim.tv_nsec = stbuf->st_mtim.tv_nsec;
	}
	if (flags & DFS_SET_ATTR_UID) {
		d_iov_set(&sg_iovs[i], &stbuf->st_uid, sizeof(uid_t));
		recxs[i].rx_idx = UID_IDX;
		recxs[i].rx_nr  = sizeof(uid_t);
		i++;
		flags &= ~DFS_SET_ATTR_UID;
		rstat.st_uid = stbuf->st_uid;
	}
	if (flags & DFS_SET_ATTR_GID) {
		d_iov_set(&sg_iovs[i], &stbuf->st_gid, sizeof(gid_t));
		recxs[i].rx_idx = GID_IDX;
		recxs[i].rx_nr  = sizeof(gid_t);
		i++;
		flags &= ~DFS_SET_ATTR_GID;
		rstat.st_gid = stbuf->st_gid;
	}
	if (flags & DFS_SET_ATTR_SIZE) {
		/* It shouldn't be possible to set the size of something which isn't a file but
		 * check here anyway, as entries which aren't files won't have array objects so
		 * check and return error here
		 */
		if (!S_ISREG(obj->mode)) {
			D_ERROR("Cannot set_size on a non file object\n");
			D_GOTO(out_obj, rc = EIO);
		}

		set_size = true;
		flags &= ~DFS_SET_ATTR_SIZE;
	}

	if (flags)
		D_GOTO(out_obj, rc = EINVAL);

	if (set_size) {
		rc = daos_array_set_size(obj->oh, th, stbuf->st_size, NULL);
		if (rc)
			D_GOTO(out_obj, rc = daos_der2errno(rc));

		/** update the returned stat buf size with the new set size */
		rstat.st_blocks = (stbuf->st_size + (1 << 9) - 1) >> 9;
		rstat.st_size   = stbuf->st_size;

		/**
		 * if mtime is set, we need to to just update the hlc on the entry. if mtime and/or
		 * ctime were not set, we need to update the stat buf returned. both cases require
		 * an array stat for the hlc.
		 */
		/** TODO - need an array API to just stat the max epoch without size */
		rc = daos_array_stat(obj->oh, th, &array_stbuf, NULL);
		if (rc)
			D_GOTO(out_obj, rc = daos_der2errno(rc));

		if (!set_mtime) {
			rc = d_hlc2timespec(array_stbuf.st_max_epoch, &rstat.st_mtim);
			if (rc) {
				D_ERROR("d_hlc2timespec() failed " DF_RC "\n", DP_RC(rc));
				D_GOTO(out_obj, rc = daos_der2errno(rc));
			}
		} else {
			D_ASSERT(hlc_recx_idx > 0);
			D_ASSERT(recxs[hlc_recx_idx].rx_idx == HLC_IDX);
			d_iov_set(&sg_iovs[hlc_recx_idx], &array_stbuf.st_max_epoch,
				  sizeof(uint64_t));
		}

		if (!set_ctime) {
			rc = d_hlc2timespec(array_stbuf.st_max_epoch, &rstat.st_ctim);
			if (rc) {
				D_ERROR("d_hlc2timespec() failed " DF_RC "\n", DP_RC(rc));
				D_GOTO(out_obj, rc = daos_der2errno(rc));
			}
		}
	}

	iod.iod_nr = i;
	if (i == 0)
		D_GOTO(out_stat, rc = 0);
	sgl.sg_nr     = i;
	sgl.sg_nr_out = 0;
	sgl.sg_iovs   = &sg_iovs[0];

	rc = daos_obj_update(oh, th, DAOS_COND_DKEY_UPDATE, &dkey, 1, &iod, &sgl, NULL);
	if (rc) {
		D_ERROR("Failed to update attr " DF_RC "\n", DP_RC(rc));
		D_GOTO(out_obj, rc = daos_der2errno(rc));
	}

out_stat:
	*stbuf = rstat;
out_obj:
	daos_obj_close(oh, NULL);
	return rc;
}

int
dfs_punch(dfs_t *dfs, dfs_obj_t *obj, daos_off_t offset, daos_size_t len)
{
	daos_size_t      size;
	daos_array_iod_t iod;
	daos_range_t     rg;
	daos_off_t       hi;
	int              rc;

	if (dfs == NULL || !dfs->mounted)
		return EINVAL;
	if (dfs->amode != O_RDWR)
		return EPERM;
	if (obj == NULL || !S_ISREG(obj->mode))
		return EINVAL;
	if ((obj->flags & O_ACCMODE) == O_RDONLY)
		return EPERM;

	/** simple truncate */
	if (len == DFS_MAX_FSIZE) {
		rc = daos_array_set_size(obj->oh, DAOS_TX_NONE, offset, NULL);
		return daos_der2errno(rc);
	}

	rc = daos_array_get_size(obj->oh, DAOS_TX_NONE, &size, NULL);
	if (rc)
		return daos_der2errno(rc);

	/** nothing to do if offset is larger or equal to the file size */
	if (size <= offset)
		return 0;

	if ((offset + len) < offset)
		hi = DFS_MAX_FSIZE;
	else
		hi = offset + len;

	/** if fsize is between the range to punch, just truncate to offset */
	if (offset < size && size <= hi) {
		rc = daos_array_set_size(obj->oh, DAOS_TX_NONE, offset, NULL);
		return daos_der2errno(rc);
	}

	D_ASSERT(size > hi);

	/** Punch offset -> len */
	iod.arr_nr  = 1;
	rg.rg_len   = len;
	rg.rg_idx   = offset;
	iod.arr_rgs = &rg;

	rc = daos_array_punch(obj->oh, DAOS_TX_NONE, &iod, NULL);
	if (rc) {
		D_ERROR("daos_array_punch() failed (%d)\n", rc);
		return daos_der2errno(rc);
	}

	return rc;
}

int
dfs_get_mode(dfs_obj_t *obj, mode_t *mode)
{
	if (obj == NULL || mode == NULL)
		return EINVAL;

	*mode = obj->mode;
	return 0;
}

int
dfs_get_symlink_value(dfs_obj_t *obj, char *buf, daos_size_t *size)
{
	daos_size_t val_size;

	if (obj == NULL || !S_ISLNK(obj->mode))
		return EINVAL;
	if (obj->value == NULL)
		return EINVAL;

	val_size = strlen(obj->value) + 1;
	if (*size == 0 || buf == NULL) {
		*size = val_size;
		return 0;
	}

	if (*size < val_size)
		strncpy(buf, obj->value, *size);
	else
		strcpy(buf, obj->value);

	*size = val_size;
	return 0;
}

int
dfs_sync(dfs_t *dfs)
{
	if (dfs == NULL || !dfs->mounted)
		return EINVAL;
	if (dfs->amode != O_RDWR)
		return EPERM;

	/** Take a snapshot here and allow rollover to that when supported. */

	return 0;
}

int
dfs_obj2id(dfs_obj_t *obj, daos_obj_id_t *oid)
{
	if (obj == NULL || oid == NULL)
		return EINVAL;
	oid_cp(oid, obj->oid);
	return 0;
}
