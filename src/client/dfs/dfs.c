/**
 * (C) Copyright 2018-2019 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */

#define D_LOGFAC	DD_FAC(dfs)

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <daos/common.h>
#include <daos/debug.h>
#include <daos/container.h>

#include "daos_types.h"
#include "daos_api.h"
#include "daos_addons.h"
#include "daos_fs.h"
#include "daos_security.h"

/** D-key name of SB info in the SB object */
#define SB_DKEY		"DFS_SB_DKEY"
/** A-key name of SB info in the SB object */
#define SB_AKEY		"DFS_SB_AKEY"
/** Magic Value for SB value */
#define SB_MAGIC	0xda05df50da05df50

/** Number of A-keys for attributes in any object entry */
#define INODE_AKEYS	5
/** A-key name of mode_t value */
#define MODE_NAME	"mode"
/** A-key name of object ID value */
#define OID_NAME	"oid"
/** A-key name of last access time */
#define ATIME_NAME	"atime"
/** A-key name of last modify time */
#define MTIME_NAME	"mtime"
/** A-key name of last change time */
#define CTIME_NAME	"ctime"
/** A-key name of symlink value */
#define SYML_NAME	"syml"

/** Array object stripe size for regular files */
#define DFS_DEFAULT_CHUNK_SIZE	1048576

/** Parameters for dkey enumeration */
#define ENUM_DESC_NR    10
#define ENUM_DESC_BUF   (ENUM_DESC_NR * DFS_MAX_PATH)

/** OIDs for Superblock and Root objects */
#define RESERVED_LO	0
#define SB_HI		0
#define ROOT_HI		1

enum {
	DFS_WRITE,
	DFS_READ
};

/** object struct that is instantiated for a DFS open object */
struct dfs_obj {
	/** DAOS object ID */
	daos_obj_id_t		oid;
	/** DAOS object open handle */
	daos_handle_t		oh;
	/** mode_t containing permissions & type */
	mode_t			mode;
	/** DAOS object ID of the parent of the object */
	daos_obj_id_t		parent_oid;
	/** entry name of the object in the parent */
	char			name[DFS_MAX_PATH + 1];
	/** Symlink value if object is a symbolic link */
	char			*value;
};

/** dfs struct that is instantiated for a mounted DFS namespace */
struct dfs {
	/** flag to indicate whether the dfs is mounted */
	bool			mounted;
	/** lock for threadsafety */
	pthread_mutex_t		lock;
	/** uid - inherited from pool. TODO - make this from container. */
	uid_t			uid;
	/** gid - inherited from pool. TODO - make this from container. */
	gid_t			gid;
	/** Access mode (RDONLY, RDWR) */
	int			amode;
	/** Open pool handle of the DFS */
	daos_handle_t		poh;
	/** Open container handle of the DFS */
	daos_handle_t		coh;
	/** Object ID reserved for this DFS (see oid_gen below) */
	daos_obj_id_t		oid;
	/** OID of SB */
	daos_obj_id_t		super_oid;
	/** Open object handle of SB */
	daos_handle_t		super_oh;
	/** Root object info */
	dfs_obj_t		root;
};

struct dfs_entry {
	/** mode (permissions + entry type) */
	mode_t		mode;
	/** Object ID if not a symbolic link */
	daos_obj_id_t	oid;
	/** Sym Link value */
	char		*value;
	/* Time of last access */
	time_t		atime;
	/* Time of last modification */
	time_t		mtime;
	/* Time of last status change */
	time_t		ctime;
};

#if 0
static void
time2str(char *buf, time_t t) {
	struct tm tm;

	gmtime_r(&t, &tm);
	strftime(buf, 26, "%F %T", &tm);
}

static time_t
str2time(char *buf) {
	time_t t;
	struct tm tm;

	strptime(buf, "%F %T", &tm);
	t = mktime(&tm);
	return t;
}

static void
print_mode(mode_t mode)
{
	D_DEBUG(DB_TRACE, "(%o)\t", mode);
	D_DEBUG(DB_TRACE, (mode & S_IRUSR) ? "r" : "-");
	D_DEBUG(DB_TRACE, (mode & S_IWUSR) ? "w" : "-");
	D_DEBUG(DB_TRACE, (mode & S_IXUSR) ? "x" : "-");
	D_DEBUG(DB_TRACE, (mode & S_IRGRP) ? "r" : "-");
	D_DEBUG(DB_TRACE, (mode & S_IWGRP) ? "w" : "-");
	D_DEBUG(DB_TRACE, (mode & S_IXGRP) ? "x" : "-");
	D_DEBUG(DB_TRACE, (mode & S_IROTH) ? "r" : "-");
	D_DEBUG(DB_TRACE, (mode & S_IWOTH) ? "w" : "-");
	D_DEBUG(DB_TRACE, (mode & S_IXOTH) ? "x" : "-");
	D_DEBUG(DB_TRACE, "\n");
}

static void
print_stat(struct stat *stbuf)
{
	char buf[26];

	D_DEBUG(DB_TRACE, "Size = %zu\n", stbuf->st_size);
	D_DEBUG(DB_TRACE, "UID %lu\n", (unsigned long int)stbuf->st_uid);
	D_DEBUG(DB_TRACE, "GID %lu\n", (unsigned long int)stbuf->st_gid);
	print_mode(stbuf->st_mode);
	time2str(buf, stbuf->st_atim.tv_sec);
	D_DEBUG(DB_TRACE, "Access time %s\n", buf);
	time2str(buf, stbuf->st_mtim.tv_sec);
	D_DEBUG(DB_TRACE, "Modify time %s\n", buf);
	time2str(buf, stbuf->st_ctim.tv_sec);
	D_DEBUG(DB_TRACE, "Change time %s\n", buf);
}
#endif

static inline int
get_daos_obj_mode(int flags)
{
	if ((flags & O_ACCMODE) == O_RDONLY)
		return DAOS_OO_RO;
	else if ((flags & O_ACCMODE) == O_RDWR ||
		 (flags & O_ACCMODE) == O_WRONLY)
		return DAOS_OO_RW;
	else
		return -1;
}

static inline void
oid_cp(daos_obj_id_t *dst, daos_obj_id_t src)
{
	dst->hi = src.hi;
	dst->lo = src.lo;
}

#define MAX_OID_HI ((1UL << 32) - 1)

/*
 * OID generation for the dfs objects.
 *
 * The oid.lo uint64_t value will be allocated from the DAOS container using the
 * unique oid allocator. 1 oid at a time will be allocated for the dfs mount.
 * The oid.hi value has the high 32 bits reserved for DAOS (obj class, type,
 * etc.). The lower 32 bits will be used locally by the dfs mount point, and
 * hence discarded when the dfs is unmounted.
 */
static int
oid_gen(dfs_t *dfs, uint16_t oclass, bool file, daos_obj_id_t *oid)
{
	daos_ofeat_t	feat = 0;
	int		rc;

	if (oclass == 0)
		oclass = DAOS_OC_REPL_MAX_RW;

	D_MUTEX_LOCK(&dfs->lock);
	/** If we ran out of local OIDs, alloc one from the container */
	if (dfs->oid.hi >= MAX_OID_HI) {
		/** Allocate an OID for the namespace */
		rc = daos_cont_alloc_oids(dfs->coh, 1, &dfs->oid.lo, NULL);
		if (rc) {
			D_ERROR("daos_cont_alloc_oids() Failed (%d)\n", rc);
			D_MUTEX_UNLOCK(&dfs->lock);
			return -daos_der2errno(rc);
		}
		dfs->oid.hi = 0;
	}

	/** set oid and lo, bump the current hi value */
	oid->lo = dfs->oid.lo;
	oid->hi = dfs->oid.hi++;
	D_MUTEX_UNLOCK(&dfs->lock);

	/** if a regular file, use UINT64 typed dkeys for the array object */
	if (file)
		feat = DAOS_OF_DKEY_UINT64 | DAOS_OF_AKEY_HASHED;

	/** generate the daos object ID (set the DAOS owned bits) */
	daos_obj_generate_id(oid, feat, oclass);

	return 0;
}

static int
fetch_entry(daos_handle_t oh, daos_handle_t th, const char *name,
	    bool fetch_sym, bool *exists, struct dfs_entry *entry)
{
	d_sg_list_t	sgls[INODE_AKEYS + 1];
	d_iov_t	sg_iovs[INODE_AKEYS + 1];
	daos_iod_t	iods[INODE_AKEYS + 1];
	char		*value = NULL;
	daos_key_t	dkey;
	unsigned int	akeys_nr, i;
	int		rc;

	D_ASSERT(name);

	/** TODO - not supported yet */
	if (strcmp(name, ".") == 0)
		D_ASSERT(0);

	d_iov_set(&dkey, (void *)name, strlen(name));
	i = 0;

	/** Set Akey for MODE */
	d_iov_set(&sg_iovs[i], &entry->mode, sizeof(mode_t));
	d_iov_set(&iods[i].iod_name, MODE_NAME, strlen(MODE_NAME));
	i++;

	/** Set Akey for OID; if entry is symlink, this value will be bogus */
	d_iov_set(&sg_iovs[i], &entry->oid, sizeof(daos_obj_id_t));
	d_iov_set(&iods[i].iod_name, OID_NAME, strlen(OID_NAME));
	i++;

	/** Set Akey for ATIME */
	d_iov_set(&sg_iovs[i], &entry->atime, sizeof(time_t));
	d_iov_set(&iods[i].iod_name, ATIME_NAME, strlen(ATIME_NAME));
	i++;

	/** Set Akey for MTIME */
	d_iov_set(&sg_iovs[i], &entry->mtime, sizeof(time_t));
	d_iov_set(&iods[i].iod_name, MTIME_NAME, strlen(MTIME_NAME));
	i++;

	/** Set Akey for CTIME */
	d_iov_set(&sg_iovs[i], &entry->ctime, sizeof(time_t));
	d_iov_set(&iods[i].iod_name, CTIME_NAME, strlen(CTIME_NAME));
	i++;

	if (fetch_sym) {
		value = malloc(PATH_MAX);
		if (value == NULL)
			return -ENOMEM;
		/** Set Akey for Symlink Value, will be empty if no symlink */
		d_iov_set(&sg_iovs[i], value, PATH_MAX);
		d_iov_set(&iods[i].iod_name, SYML_NAME, strlen(SYML_NAME));
		i++;
	}

	akeys_nr = i;

	for (i = 0; i < akeys_nr; i++) {
		sgls[i].sg_nr		= 1;
		sgls[i].sg_nr_out	= 0;
		sgls[i].sg_iovs		= &sg_iovs[i];

		daos_csum_set(&iods[i].iod_kcsum, NULL, 0);
		iods[i].iod_nr		= 1;
		iods[i].iod_size	= DAOS_REC_ANY;
		iods[i].iod_recxs	= NULL;
		iods[i].iod_eprs	= NULL;
		iods[i].iod_csums	= NULL;
		iods[i].iod_type	= DAOS_IOD_SINGLE;
	}

	rc = daos_obj_fetch(oh, th, &dkey, akeys_nr, iods, sgls, NULL, NULL);
	if (rc) {
		D_ERROR("Failed to fetch entry %s (%d)\n", name, rc);
		D_GOTO(out, rc = -daos_der2errno(rc));
	}

	if (fetch_sym && S_ISLNK(entry->mode)) {
		size_t sym_len = iods[INODE_AKEYS].iod_size;

		if (sym_len != 0) {
			D_ASSERT(value);
			entry->value = strdup(value);
			if (entry->value == NULL)
				D_GOTO(out, rc = -ENOMEM);
		}
	}

	if (iods[0].iod_size == 0)
		*exists = false;
	else
		*exists = true;

out:
	if (fetch_sym)
		D_FREE(value);
	return rc;
}

static int
remove_entry(dfs_t *dfs, daos_handle_t th, daos_handle_t parent_oh,
	     const char *name, struct dfs_entry entry)
{
	daos_key_t	dkey;
	int		rc;

	if (!S_ISLNK(entry.mode)) {
		daos_handle_t oh;

		rc = daos_obj_open(dfs->coh, entry.oid, DAOS_OO_RW, &oh, NULL);
		if (rc)
			return -daos_der2errno(rc);

		rc = daos_obj_punch(oh, th, NULL);
		if (rc) {
			daos_obj_close(oh, NULL);
			return -daos_der2errno(rc);
		}

		rc = daos_obj_close(oh, NULL);
		if (rc)
			return -daos_der2errno(rc);
	}

	d_iov_set(&dkey, (void *)name, strlen(name));
	rc = daos_obj_punch_dkeys(parent_oh, th, 1, &dkey, NULL);
	return -daos_der2errno(rc);
}

static int
insert_entry(daos_handle_t oh, daos_handle_t th, const char *name,
	     struct dfs_entry entry)
{
	d_sg_list_t	sgls[INODE_AKEYS];
	d_iov_t	sg_iovs[INODE_AKEYS];
	daos_iod_t	iods[INODE_AKEYS];
	daos_key_t	dkey;
	unsigned int	akeys_nr, i;
	int		rc;

	d_iov_set(&dkey, (void *)name, strlen(name));

	i = 0;

	/** Add the mode */
	d_iov_set(&sg_iovs[i], &entry.mode, sizeof(mode_t));
	d_iov_set(&iods[i].iod_name, MODE_NAME, strlen(MODE_NAME));
	iods[i].iod_size = sizeof(mode_t);
	i++;

	/** If entry is a symlink add the value, otherwise add the oid */
	if (S_ISLNK(entry.mode)) {
		d_iov_set(&sg_iovs[i], entry.value, strlen(entry.value) + 1);
		d_iov_set(&iods[i].iod_name, SYML_NAME, strlen(SYML_NAME));
		iods[i].iod_size = strlen(entry.value) + 1;
	} else {
		d_iov_set(&sg_iovs[i], &entry.oid, sizeof(daos_obj_id_t));
		d_iov_set(&iods[i].iod_name, OID_NAME, strlen(OID_NAME));
		iods[i].iod_size = sizeof(daos_obj_id_t);
	}
	i++;

	/** Add the access time */
	d_iov_set(&sg_iovs[i], &entry.atime, sizeof(time_t));
	d_iov_set(&iods[i].iod_name, ATIME_NAME, strlen(ATIME_NAME));
	iods[i].iod_size = sizeof(time_t);
	i++;

	/** Add the modify time */
	d_iov_set(&sg_iovs[i], &entry.mtime, sizeof(time_t));
	d_iov_set(&iods[i].iod_name, MTIME_NAME, strlen(MTIME_NAME));
	iods[i].iod_size = sizeof(time_t);
	i++;

	/** Add the change time */
	d_iov_set(&sg_iovs[i], &entry.ctime, sizeof(time_t));
	d_iov_set(&iods[i].iod_name, CTIME_NAME, strlen(CTIME_NAME));
	iods[i].iod_size = sizeof(time_t);
	i++;

	akeys_nr = i;

	for (i = 0; i < akeys_nr; i++) {
		sgls[i].sg_nr		= 1;
		sgls[i].sg_nr_out	= 0;
		sgls[i].sg_iovs		= &sg_iovs[i];

		daos_csum_set(&iods[i].iod_kcsum, NULL, 0);
		iods[i].iod_nr		= 1;
		iods[i].iod_recxs	= NULL;
		iods[i].iod_eprs	= NULL;
		iods[i].iod_csums	= NULL;
		iods[i].iod_type	= DAOS_IOD_SINGLE;
	}

	rc = daos_obj_update(oh, th, &dkey, akeys_nr, iods, sgls, NULL);
	if (rc) {
		D_ERROR("Failed to insert entry %s (%d)\n", name, rc);
		return -daos_der2errno(rc);
	}

	return 0;
}

static int
get_nlinks(daos_handle_t oh, daos_handle_t th, uint32_t *nlinks,
	   bool check_empty)
{
	daos_key_desc_t	kds[ENUM_DESC_NR];
	daos_anchor_t	anchor = {0};
	uint32_t	key_nr = 0;
	d_sg_list_t	sgl;
	d_iov_t	iov;
	char		enum_buf[ENUM_DESC_BUF] = {0};

	sgl.sg_nr = 1;
	sgl.sg_nr_out = 0;
	d_iov_set(&iov, enum_buf, ENUM_DESC_BUF);
	sgl.sg_iovs = &iov;

	/** TODO - Enum of links is expensive. Need to make this faster */
	while (!daos_anchor_is_eof(&anchor)) {
		uint32_t	number = ENUM_DESC_NR;
		int		rc;

		rc = daos_obj_list_dkey(oh, th, &number, kds, &sgl, &anchor,
					NULL);
		if (rc)
			return -daos_der2errno(rc);

		if (number == 0)
			continue;

		key_nr += number;

		/** if we just want to check if entries exist, break now */
		if (check_empty)
			break;
	}

	*nlinks = key_nr;
	return 0;
}

static int
entry_stat(dfs_t *dfs, daos_handle_t th, daos_handle_t oh, const char *name,
	   struct stat *stbuf)
{
	struct dfs_entry	entry = {0};
	bool			exists;
	daos_size_t		size;
	uint32_t		nlinks;
	int			rc;

	memset(stbuf, 0, sizeof(struct stat));

	/* Check if parent has the entry */
	rc = fetch_entry(oh, th, name, true, &exists, &entry);
	if (rc)
		return rc;

	if (!exists)
		return -ENOENT;

	switch (entry.mode & S_IFMT) {
	case S_IFDIR:
	{
		daos_handle_t	dir_oh;

		size = sizeof(entry);
		rc = daos_obj_open(dfs->coh, entry.oid, DAOS_OO_RO,
				   &dir_oh, NULL);
		if (rc)
			return -daos_der2errno(rc);

		/*
		 * TODO - This makes stat very slow now. Need to figure out a
		 * different way to get/maintain nlinks.
		 */
		rc = get_nlinks(dir_oh, th, &nlinks, false);
		if (rc) {
			daos_obj_close(dir_oh, NULL);
			return rc;
		}

		rc = daos_obj_close(dir_oh, NULL);
		if (rc)
			return -daos_der2errno(rc);
		break;
	}
	case S_IFREG:
	{
		daos_handle_t	file_oh;
		daos_size_t	elem_size, chunk_size;

		rc = daos_array_open(dfs->coh, entry.oid, th, DAOS_OO_RO,
				     &elem_size, &chunk_size, &file_oh, NULL);
		if (rc) {
			D_ERROR("daos_array_open() failed (%d)\n", rc);
			return -daos_der2errno(rc);
		}
		if (elem_size != 1) {
			daos_array_close(file_oh, NULL);
			D_ERROR("Elem size is not 1 in a byte array (%zu)\n",
				 elem_size);
			return -EINVAL;
		}

		rc = daos_array_get_size(file_oh, th, &size, NULL);
		if (rc) {
			daos_array_close(file_oh, NULL);
			return -daos_der2errno(rc);
		}

		rc = daos_array_close(file_oh, NULL);
		if (rc)
			return -daos_der2errno(rc);

		nlinks = 1;

		/*
		 * TODO - this is not accurate since it does not account for
		 * sparse files or file metadata or xattributes.
		 */
		stbuf->st_blocks = (size + (1 << 9) - 1) >> 9;
		break;
	}
	case S_IFLNK:
		size = strlen(entry.value);
		D_FREE(entry.value);
		nlinks = 1;
		break;
	default:
		D_ERROR("Invalid entry type (not a dir, file, symlink).\n");
		return -EINVAL;
	}

	stbuf->st_nlink = (nlink_t)nlinks;
	stbuf->st_size = size;
	stbuf->st_mode = entry.mode;
	stbuf->st_uid = dfs->uid;
	stbuf->st_gid = dfs->gid;
	stbuf->st_atim.tv_sec = entry.atime;
	stbuf->st_mtim.tv_sec = entry.mtime;
	stbuf->st_ctim.tv_sec = entry.ctime;

	return 0;
}

static inline int
check_name(const char *name)
{
	if (name == NULL || strchr(name, '/'))
		return -EINVAL;
	if (strlen(name) > DFS_MAX_PATH)
		return -EINVAL;
	return 0;
}

static int
check_access(dfs_t *dfs, uid_t uid, gid_t gid, mode_t mode, int mask)
{
	mode_t	base_mask;

	/** Root can access everything */
	if (uid == 0)
		return 0;

	if (mode == 0)
		return -EPERM;

	/** set base_mask to others at first step */
	base_mask = S_IRWXO;
	/** update base_mask if uid matches */
	if (uid == dfs->uid)
		base_mask |= S_IRWXU;
	/** update base_mask if gid matches */
	if (gid == dfs->gid)
		base_mask |= S_IRWXG;

	/** AND the object mode with the base_mask to determine access */
	mode &= base_mask;

	/** Execute check */
	if (X_OK == (mask & X_OK))
		if (0 == (mode & (S_IXUSR | S_IXGRP | S_IXOTH)))
			return -EPERM;

	/** Write check */
	if (W_OK == (mask & W_OK))
		if (0 == (mode & (S_IWUSR | S_IWGRP | S_IWOTH)))
			return -EPERM;

	/** Read check */
	if (R_OK == (mask & R_OK))
		if (0 == (mode & (S_IRUSR | S_IRGRP | S_IROTH)))
			return -EPERM;

	/** TODO - check ACL, attributes (immutable, append) etc. */
	return 0;
}

static int
open_file(dfs_t *dfs, daos_handle_t th, dfs_obj_t *parent, int flags,
	  daos_oclass_id_t cid, daos_size_t chunk_size, dfs_obj_t *file)
{
	struct dfs_entry	entry = {0};
	bool			exists;
	daos_size_t		elem_size;
	int			daos_mode;
	int			rc;

	/* Check if parent has the filename entry */
	rc = fetch_entry(parent->oh, th, file->name, false, &exists, &entry);
	if (rc)
		return rc;

	if (flags & O_CREAT) {
		if (exists) {
			if (flags & O_EXCL)
				return -EEXIST;

			if (S_ISDIR(entry.mode)) {
				D_DEBUG(DB_TRACE, "can't overwrite dir %s with "
					"non-directory\n", file->name);
				return -EINVAL;
			}

			goto open_file;
		}

		/** Get new OID for the file */
		rc = oid_gen(dfs, cid, true, &file->oid);
		if (rc != 0)
			return rc;
		oid_cp(&entry.oid, file->oid);

		/** Create array object for the file */
		rc = daos_array_create(dfs->coh, file->oid, th, 1,
				       (chunk_size ? chunk_size :
					DFS_DEFAULT_CHUNK_SIZE),
				       &file->oh, NULL);
		if (rc != 0) {
			D_ERROR("daos_array_create() failed (%d)\n", rc);
			return -daos_der2errno(rc);
		}

		/** Create and insert entry in parent dir object. */
		entry.mode = file->mode;
		entry.atime = entry.mtime = entry.ctime = time(NULL);

		rc = insert_entry(parent->oh, th, file->name, entry);
		if (rc != 0) {
			daos_obj_close(file->oh, NULL);
			D_ERROR("Inserting file entry %s failed (%d)\n",
				file->name, rc);
			return rc;
		}

		return rc;
	}

	/** Open the byte array */
	if (!exists)
		return -ENOENT;

open_file:
	if (!S_ISREG(entry.mode)) {
		if (entry.value) {
			D_ASSERT(S_ISLNK(entry.mode));
			D_FREE(entry.value);
		}
		return -EINVAL;
	}

	daos_mode = get_daos_obj_mode(flags);
	if (daos_mode == -1)
		return -EINVAL;

	rc = check_access(dfs, geteuid(), getegid(), entry.mode,
			  (daos_mode == DAOS_OO_RO) ? R_OK : R_OK | W_OK);
	if (rc)
		return rc;

	file->mode = entry.mode;
	rc = daos_array_open(dfs->coh, entry.oid, th, daos_mode,
			     &elem_size, &chunk_size, &file->oh, NULL);
	if (rc != 0) {
		D_ERROR("daos_array_open() failed (%d)\n", rc);
		return -daos_der2errno(rc);
	}
	if (elem_size != 1) {
		daos_array_close(file->oh, NULL);
		D_ERROR("Elem size is not 1 in a byte array (%zu)\n",
			elem_size);
		return -EINVAL;
	}
	oid_cp(&file->oid, entry.oid);

	return 0;
}

/*
 * create a dir object. If caller passes parent obj, we check for existence of
 * object first.
 */
static int
create_dir(dfs_t *dfs, daos_handle_t th, daos_handle_t parent_oh,
	   daos_oclass_id_t cid, dfs_obj_t *dir)
{
	struct dfs_entry	entry;
	bool			exists;
	int			rc;

	if (!daos_handle_is_inval(parent_oh)) {
		/* Check if parent has the dirname entry */
		rc = fetch_entry(parent_oh, th, dir->name, false, &exists,
				 &entry);
		if (rc)
			return rc;

		if (exists)
			return -EEXIST;
	}

	rc = oid_gen(dfs, cid, false, &dir->oid);
	if (rc != 0)
		return rc;
	rc = daos_obj_open(dfs->coh, dir->oid, DAOS_OO_RW, &dir->oh, NULL);
	if (rc) {
		D_ERROR("daos_obj_open() Failed (%d)\n", rc);
		return -daos_der2errno(rc);
	}

	return 0;
}

static int
open_dir(dfs_t *dfs, daos_handle_t th, daos_handle_t parent_oh, int flags,
	 daos_oclass_id_t cid, dfs_obj_t *dir)
{
	struct dfs_entry	entry;
	bool			exists;
	int			daos_mode;
	int			rc;

	if (flags & O_CREAT) {
		rc = create_dir(dfs, th, parent_oh, cid, dir);
		if (rc)
			return rc;

		entry.oid = dir->oid;
		entry.mode = dir->mode;
		entry.atime = entry.mtime = entry.ctime = time(NULL);

		rc = insert_entry(parent_oh, th, dir->name, entry);
		if (rc != 0) {
			daos_obj_close(dir->oh, NULL);
			D_ERROR("Inserting dir entry %s failed (%d)\n",
				dir->name, rc);
		}

		return rc;
	}

	daos_mode = get_daos_obj_mode(flags);
	if (daos_mode == -1)
		return -EINVAL;

	/* Check if parent has the dirname entry */
	rc = fetch_entry(parent_oh, th, dir->name, false, &exists, &entry);
	if (rc)
		return rc;

	if (!exists)
		return -ENOENT;

	if (!S_ISDIR(entry.mode))
		return -ENOTDIR;

	rc = check_access(dfs, geteuid(), getegid(), entry.mode,
			  (daos_mode == DAOS_OO_RO) ? R_OK : R_OK | W_OK);
	if (rc)
		return rc;

	rc = daos_obj_open(dfs->coh, entry.oid, daos_mode, &dir->oh, NULL);
	if (rc) {
		D_ERROR("daos_obj_open() Failed (%d)\n", rc);
		return -daos_der2errno(rc);
	}
	dir->mode = entry.mode;
	oid_cp(&dir->oid, entry.oid);

	return 0;
}

static int
open_symlink(dfs_t *dfs, daos_handle_t th, dfs_obj_t *parent, int flags,
	     const char *value, dfs_obj_t *sym)
{
	struct dfs_entry	entry;
	bool			exists;
	int			rc;

	/* Check if parent has the symlink entry */
	rc = fetch_entry(parent->oh, th, sym->name, false, &exists, &entry);
	if (rc)
		return rc;

	if (flags & O_CREAT) {
		if (exists)
			return -EEXIST;

		entry.value = (char *)value;
		entry.oid.hi = 0;
		entry.oid.lo = 0;
		entry.mode = sym->mode;
		entry.atime = entry.mtime = entry.ctime = time(NULL);

		rc = insert_entry(parent->oh, th, sym->name, entry);
		if (rc)
			D_ERROR("Inserting entry %s failed (rc = %d)\n",
				sym->name, rc);
		return rc;
	}

	D_ASSERT(0);
	if (!exists)
		return -ENOENT;
	return rc;
}

static int
check_sb(dfs_t *dfs, daos_handle_t th, bool insert, bool *exists)
{
	d_sg_list_t	sgl;
	d_iov_t	sg_iov;
	daos_iod_t	iod;
	daos_key_t	dkey;
	uint64_t	sb_magic;
	int		rc;

	d_iov_set(&dkey, SB_DKEY, strlen(SB_DKEY));

	d_iov_set(&sg_iov, &sb_magic, sizeof(uint64_t));
	sgl.sg_nr = 1;
	sgl.sg_nr_out = 0;
	sgl.sg_iovs = &sg_iov;

	d_iov_set(&iod.iod_name, SB_AKEY, strlen(SB_AKEY));
	daos_csum_set(&iod.iod_kcsum, NULL, 0);
	iod.iod_nr	= 1;
	iod.iod_size	= DAOS_REC_ANY;
	iod.iod_recxs	= NULL;
	iod.iod_eprs	= NULL;
	iod.iod_csums	= NULL;
	iod.iod_type	= DAOS_IOD_SINGLE;

	rc = daos_obj_fetch(dfs->super_oh, th, &dkey, 1, &iod, &sgl,
			    NULL, NULL);
	if (rc) {
		D_ERROR("Failed to fetch SB info (%d)\n", rc);
		return -daos_der2errno(rc);
	}

	/** check if SB info exists */
	if (iod.iod_size == 0) {
		*exists = false;

		/** if insert option is set, then insert the SB */
		if (!insert)
			return 0;
		iod.iod_size = sizeof(uint64_t);
		sb_magic = SB_MAGIC;
		rc = daos_obj_update(dfs->super_oh, th, &dkey, 1, &iod, &sgl,
				     NULL);
		if (rc) {
			D_ERROR("Failed to update SB info (%d)\n", rc);
			return -daos_der2errno(rc);
		}
	} else {
		if (sb_magic != SB_MAGIC) {
			D_ERROR("SB MAGIC verification failed\n");
			return -EINVAL;
		}

		*exists = true;
	}

	return 0;
}

int
dfs_mount(daos_handle_t poh, daos_handle_t coh, int flags, dfs_t **_dfs)
{
	dfs_t			*dfs;
	daos_handle_t		th;
	daos_pool_info_t	pool_info;
	daos_prop_t		*prop;
	struct daos_prop_entry	*prop_entry;
	struct dfs_entry	entry = {0};
	bool			sb_exists;
	int			amode, obj_mode;
	int			rc;

	amode = (flags & O_ACCMODE);
	obj_mode = get_daos_obj_mode(flags);
	if (obj_mode == -1)
		return -EINVAL;

	D_ALLOC_PTR(dfs);
	if (dfs == NULL)
		return -ENOMEM;

	dfs->poh = poh;
	dfs->coh = coh;
	dfs->amode = amode;
	rc = D_MUTEX_INIT(&dfs->lock, NULL);
	if (rc != 0)
		return -daos_der2errno(rc);

	/* Fetch ownership props with the query */
	prop = daos_prop_alloc(0);
	if (prop == NULL) {
		D_ERROR("Failed to allocate pool prop for query\n");
		rc = -DER_NOMEM;
		D_GOTO(err_dfs, rc);
	}

	rc = daos_pool_query(poh, NULL, &pool_info, prop, NULL);
	if (rc) {
		D_ERROR("daos_pool_query() Failed (%d)\n", rc);
		D_GOTO(err_prop, rc = -daos_der2errno(rc));
	}

	/* Convert the owner information to uid/gid */
	prop_entry = daos_prop_entry_get(prop, DAOS_PROP_PO_OWNER);
	D_ASSERT(prop_entry != NULL);
	rc = daos_acl_principal_to_uid(prop_entry->dpe_str, &dfs->uid);
	if (rc != 0) {
		D_ERROR("Unable to convert owner to uid\n");
		D_GOTO(err_prop, rc = -daos_der2errno(rc));
	}

	prop_entry = daos_prop_entry_get(prop, DAOS_PROP_PO_OWNER_GROUP);
	D_ASSERT(prop_entry != NULL);
	rc = daos_acl_principal_to_gid(prop_entry->dpe_str, &dfs->gid);
	if (rc != 0) {
		D_ERROR("Unable to convert owner-group to gid\n");
		D_GOTO(err_prop, rc = -daos_der2errno(rc));
	}

	/** if mount RW, create TX */
	if (amode == O_RDWR) {
		rc = daos_tx_open(coh, &th, NULL);
		if (rc) {
			D_ERROR("daos_tx_open() Failed (%d)\n", rc);
			D_GOTO(err_prop, rc = -daos_der2errno(rc));
		}
	} else if (amode == O_RDONLY) {
		th = DAOS_TX_NONE;
	} else {
		D_ERROR("Invalid dfs_mount access mode\n");
		D_GOTO(err_prop, rc = -EINVAL);
	}

	dfs->oid.hi = 0;
	dfs->oid.lo = 0;

	/** Open special object on container for SB info */
	dfs->super_oid.lo = RESERVED_LO;
	dfs->super_oid.hi = SB_HI;
	daos_obj_generate_id(&dfs->super_oid, 0, DAOS_OC_REPL_MAX_RW);

	rc = daos_obj_open(coh, dfs->super_oid, obj_mode, &dfs->super_oh, NULL);
	if (rc) {
		D_ERROR("daos_obj_open() Failed (%d)\n", rc);
		D_GOTO(err_tx, rc = -daos_der2errno(rc));
	}

	D_DEBUG(DB_TRACE, "DFS super object %"PRIu64".%"PRIu64"\n",
		dfs->super_oid.hi, dfs->super_oid.lo);

	/** if RW, allocate an OID for the namespace */
	if (amode == O_RDWR) {
		rc = daos_cont_alloc_oids(coh, 1, &dfs->oid.lo, NULL);
		if (rc) {
			D_ERROR("daos_cont_alloc_oids() Failed (%d)\n", rc);
			D_GOTO(err_tx, rc = -daos_der2errno(rc));
		}
	}

	/** Check if SB object exists already, and create it if it doesn't */
	rc = check_sb(dfs, th, (amode == O_RDWR), &sb_exists);
	if (rc)
		D_GOTO(err_tx, rc);

	/** Check if super object has the root entry */
	strcpy(dfs->root.name, "/");
	oid_cp(&dfs->root.parent_oid, dfs->super_oid);

	rc = open_dir(dfs, th, dfs->super_oh, amode, 0, &dfs->root);
	if (rc == 0) {
		/** OID should not be 0 since this is an existing namespace. */
		if (sb_exists && amode == O_RDWR &&
		    dfs->oid.lo == RESERVED_LO) {
			D_ERROR("OID should not be 0 in existing namespace\n");
			D_GOTO(err_super, rc = -EINVAL);
		}
		D_DEBUG(DB_TRACE, "Namespace exists. OID lo = %"PRIu64".\n",
			dfs->oid.lo);
	} else if (rc == -ENOENT) {
		if (amode == O_RDWR) {
			/*
			 * Set hi when we allocate the reserved oid. Account 0
			 * for SB, 1 for root obj.
			 */
			if (dfs->oid.lo == RESERVED_LO)
				dfs->oid.hi = ROOT_HI + 1;
			/*
			 * if lo is not 0 (reserved), we can use all the hi
			 * ranks and we can start hi from 0. This happens when
			 * multiple independent access is mounting the dfs at
			 * the same time, and races between SB creation and oid
			 * allocation can happen, leading us to here where the
			 * mounter did not see the SB, but another one has raced
			 * and allocated oid 0. One example is the MPI-IO file
			 * per process case, where every process creates it's
			 * own file in the same flat namespace.
			 */
			else
				dfs->oid.hi = 0;
		} else {
			dfs->oid.hi = MAX_OID_HI;
		}

		/** Create the root object */
		dfs->root.mode = S_IFDIR | 0777;
		dfs->root.oid.lo = RESERVED_LO;
		dfs->root.oid.hi = ROOT_HI;
		daos_obj_generate_id(&dfs->root.oid, 0, DAOS_OC_REPL_MAX_RW);

		rc = daos_obj_open(coh, dfs->root.oid, obj_mode, &dfs->root.oh,
				   NULL);
		if (rc) {
			D_ERROR("Failed to open root dir object (%d).", rc);
			D_GOTO(err_super, rc = -daos_der2errno(rc));
		}

		/** Insert root entry in the SB */
		oid_cp(&entry.oid, dfs->root.oid);
		entry.mode = S_IFDIR | 0777;
		entry.atime = entry.mtime = entry.ctime = time(NULL);
		rc = insert_entry(dfs->super_oh, th, dfs->root.name, entry);
		if (rc) {
			D_ERROR("Failed to insert root entry (%d).", rc);
			D_GOTO(err_root, rc);
		}

		D_DEBUG(DB_TRACE, "Created root object %"PRIu64".%"PRIu64"\n",
			dfs->root.oid.hi, dfs->root.oid.lo);
	} else {
		D_ERROR("Failed to create/open root object\n");
		D_GOTO(err_super, rc);
	}

	if (amode == O_RDWR) {
		rc = daos_tx_commit(th, NULL);
		if (rc) {
			D_ERROR("TX commit failed (rc = %d)\n", rc);
			D_GOTO(err_root, rc = -daos_der2errno(rc));
		}
		rc = daos_tx_close(th, NULL);
		if (rc) {
			D_ERROR("TX close failed (rc = %d)\n", rc);
			D_GOTO(err_root, rc = -daos_der2errno(rc));
		}
	}

	dfs->mounted = true;
	*_dfs = dfs;

	return rc;
err_root:
	daos_obj_punch(dfs->root.oh, th, NULL);
	daos_obj_close(dfs->root.oh, NULL);
err_super:
	daos_obj_punch(dfs->super_oh, th, NULL);
	daos_obj_close(dfs->super_oh, NULL);
err_tx:
	if (amode == O_RDWR) {
		daos_tx_abort(th, NULL);
		daos_tx_close(th, NULL);
	}
err_prop:
	daos_prop_free(prop);
err_dfs:
	D_FREE(dfs);
	return rc;
}

int
dfs_umount(dfs_t *dfs)
{
	if (dfs == NULL || !dfs->mounted)
		return -EINVAL;

	daos_obj_close(dfs->root.oh, NULL);
	daos_obj_close(dfs->super_oh, NULL);

	D_MUTEX_DESTROY(&dfs->lock);
	D_FREE(dfs);

	return 0;
}

int
dfs_get_file_oh(dfs_obj_t *obj, daos_handle_t *oh)
{
	if (obj == NULL || !S_ISREG(obj->mode))
		return -EINVAL;
	if (oh == NULL)
		return -EINVAL;

	oh->cookie = obj->oh.cookie;
	return 0;
}

int
dfs_mkdir(dfs_t *dfs, dfs_obj_t *parent, const char *name, mode_t mode)
{
	dfs_obj_t		new_dir;
	daos_handle_t		th = DAOS_TX_NONE;
	struct dfs_entry	entry = {0};
	int			rc;

	if (dfs == NULL || !dfs->mounted)
		return -EINVAL;
	if (dfs->amode != O_RDWR)
		return -EPERM;
	if (parent == NULL)
		parent = &dfs->root;
	else if (!S_ISDIR(parent->mode))
		return -ENOTDIR;

	rc = check_name(name);
	if (rc)
		return rc;

	rc = check_access(dfs, geteuid(), getegid(), parent->mode, W_OK | X_OK);
	if (rc)
		return rc;

	strncpy(new_dir.name, name, DFS_MAX_PATH);
	new_dir.name[DFS_MAX_PATH] = '\0';
	rc = create_dir(dfs, th, (parent ? parent->oh : DAOS_HDL_INVAL), 0,
			&new_dir);
	if (rc)
		D_GOTO(out, rc);

	entry.oid = new_dir.oid;
	entry.mode = S_IFDIR | mode;
	entry.atime = entry.mtime = entry.ctime = time(NULL);

	rc = insert_entry(parent->oh, th, name, entry);
	if (rc != 0)
		D_GOTO(out, rc);

	daos_obj_close(new_dir.oh, NULL);

out:
	return rc;
}

static int
remove_dir_contents(dfs_t *dfs, daos_handle_t th, struct dfs_entry entry)
{
	daos_handle_t	oh;
	daos_key_desc_t	kds[ENUM_DESC_NR];
	daos_anchor_t	anchor = {0};
	d_iov_t	iov;
	char		enum_buf[ENUM_DESC_BUF] = {0};
	d_sg_list_t	sgl;
	int		rc;

	D_ASSERT(S_ISDIR(entry.mode));

	rc = daos_obj_open(dfs->coh, entry.oid, DAOS_OO_RW, &oh, NULL);
	if (rc)
		return -daos_der2errno(rc);

	sgl.sg_nr = 1;
	sgl.sg_nr_out = 0;
	d_iov_set(&iov, enum_buf, ENUM_DESC_BUF);
	sgl.sg_iovs = &iov;

	while (!daos_anchor_is_eof(&anchor)) {
		uint32_t	number = ENUM_DESC_NR;
		uint32_t	i;
		char		*ptr;

		rc = daos_obj_list_dkey(oh, th, &number, kds, &sgl, &anchor,
					NULL);
		if (rc)
			D_GOTO(out, rc = -daos_der2errno(rc));

		if (number == 0)
			continue;

		for (ptr = enum_buf, i = 0; i < number; i++) {
			struct dfs_entry child_entry;
			char entry_name[DFS_MAX_PATH + 1];
			bool exists;

			snprintf(entry_name, kds[i].kd_key_len + 1, "%s", ptr);
			ptr += kds[i].kd_key_len;

			rc = fetch_entry(oh, th, entry_name, false,
					 &exists, &child_entry);
			if (rc)
				D_GOTO(out, rc);

			D_ASSERT(exists);

			if (S_ISDIR(child_entry.mode)) {
				rc = remove_dir_contents(dfs, th, child_entry);
				if (rc)
					D_GOTO(out, rc);
			}

			rc = remove_entry(dfs, th, oh, entry_name, child_entry);
			if (rc)
				D_GOTO(out, rc);
		}
	}

out:
	daos_obj_close(oh, NULL);
	return rc;
}

int
dfs_remove(dfs_t *dfs, dfs_obj_t *parent, const char *name, bool force)
{
	struct dfs_entry	entry = {0};
	daos_handle_t           th = DAOS_TX_NONE;
	bool			exists;
	int			rc;

	if (dfs == NULL || !dfs->mounted)
		return -EINVAL;
	if (dfs->amode != O_RDWR)
		return -EPERM;
	if (parent == NULL)
		parent = &dfs->root;
	else if (!S_ISDIR(parent->mode))
		return -ENOTDIR;

	rc = check_name(name);
	if (rc)
		return rc;
	rc = check_access(dfs, geteuid(), getegid(), parent->mode, W_OK | X_OK);
	if (rc)
		return rc;

	rc = fetch_entry(parent->oh, th, name, false, &exists, &entry);
	if (rc)
		D_GOTO(out, rc);

	if (!exists)
		D_GOTO(out, rc = -ENOENT);

	if (S_ISDIR(entry.mode)) {
		uint32_t nlinks = 0;
		daos_handle_t oh;

		/** check if dir is empty */
		rc = daos_obj_open(dfs->coh, entry.oid, DAOS_OO_RW, &oh, NULL);
		if (rc) {
			D_ERROR("daos_obj_open() Failed (%d)\n", rc);
			D_GOTO(out, rc = -daos_der2errno(rc));
		}

		rc = get_nlinks(oh, th, &nlinks, true);
		if (rc) {
			daos_obj_close(oh, NULL);
			D_GOTO(out, rc);
		}

		rc = daos_obj_close(oh, NULL);
		if (rc)
			D_GOTO(out, rc = -daos_der2errno(rc));

		if (!force && nlinks != 0)
			D_GOTO(out, rc = -ENOTEMPTY);

		if (force && nlinks != 0) {
			rc = remove_dir_contents(dfs, th, entry);
			if (rc)
				D_GOTO(out, rc);
		}
	}

	rc = remove_entry(dfs, th, parent->oh, name, entry);
	if (rc)
		D_GOTO(out, rc);

out:
	return rc;
}

int
dfs_lookup(dfs_t *dfs, const char *path, int flags, dfs_obj_t **_obj,
	   mode_t *mode)
{
	dfs_obj_t		parent;
	dfs_obj_t		*obj = NULL;
	struct dfs_entry	entry = {0};
	char			*token;
	char			*rem, *sptr;
	bool			exists;
	int			daos_mode;
	uid_t			uid = geteuid();
	gid_t			gid = getegid();
	int			rc;

	if (dfs == NULL || !dfs->mounted)
		return -EINVAL;
	if (_obj == NULL)
		return -EINVAL;
	if (path == NULL)
		return -EINVAL;

	daos_mode = get_daos_obj_mode(flags);
	if (daos_mode == -1)
		return -EINVAL;

	rem = strdup(path);
	if (rem == NULL)
		return -ENOMEM;

	D_ALLOC_PTR(obj);
	if (obj == NULL)
		D_GOTO(out, rc = -ENOMEM);

	oid_cp(&obj->oid, dfs->root.oid);
	oid_cp(&obj->parent_oid, dfs->root.parent_oid);
	obj->mode = dfs->root.mode;
	strncpy(obj->name, dfs->root.name, DFS_MAX_PATH);
	obj->name[DFS_MAX_PATH] = '\0';
	rc = daos_obj_open(dfs->coh, obj->oid, daos_mode, &obj->oh, NULL);
	if (rc)
		D_GOTO(err_obj, rc = -daos_der2errno(rc));

	parent.oh = obj->oh;
	parent.mode = obj->mode;
	oid_cp(&parent.oid, obj->oid);
	oid_cp(&parent.parent_oid, obj->parent_oid);

	/** get the obj entry in the path */
	for (token = strtok_r(rem, "/", &sptr);
	     token != NULL;
	     token = strtok_r(NULL, "/", &sptr)) {
dfs_lookup_loop:

		rc = check_access(dfs, uid, gid, parent.mode, X_OK);
		if (rc)
			return rc;

		rc = fetch_entry(parent.oh, DAOS_TX_NONE, token, true,
				 &exists, &entry);
		if (rc)
			D_GOTO(err_obj, rc);

		rc = daos_obj_close(obj->oh, NULL);
		if (rc) {
			D_ERROR("daos_obj_close() Failed (%d)\n", rc);
			D_GOTO(err_obj, rc = -daos_der2errno(rc));
		}

		if (!exists)
			D_GOTO(err_obj, rc = -ENOENT);

		oid_cp(&obj->oid, entry.oid);
		oid_cp(&obj->parent_oid, parent.oid);
		strncpy(obj->name, token, DFS_MAX_PATH);
		obj->name[DFS_MAX_PATH] = '\0';
		obj->mode = entry.mode;

		/** if entry is a file, open the array object and return */
		if (S_ISREG(entry.mode)) {
			daos_size_t elem_size, chunk_size;

			/* if there are more entries, then file is not a dir */
			if (strtok_r(NULL, "/", &sptr) != NULL) {
				D_ERROR("%s is not a directory\n", obj->name);
				D_GOTO(err_obj, rc = -ENOENT);
			}

			obj->mode = entry.mode;
			rc = daos_array_open(dfs->coh, entry.oid,
					     DAOS_TX_NONE, daos_mode,
					     &elem_size, &chunk_size, &obj->oh,
					     NULL);
			if (rc != 0) {
				D_ERROR("daos_array_open() failed (%d)\n", rc);
				D_GOTO(err_obj, rc = -daos_der2errno(rc));
			}
			if (elem_size != 1) {
				D_ERROR("Invalid Byte array elem size (%zu)\n",
					elem_size);
				daos_array_close(obj->oh, NULL);
				D_GOTO(err_obj, -EINVAL);
			}

			break;
		}

		if (S_ISLNK(entry.mode)) {
			obj->mode = entry.mode;
			obj->value = entry.value;

			/*
			 * If there is a token after the sym link entry, treat
			 * the sym link as a directory and look up it's value.
			 */
			token = strtok_r(NULL, "/", &sptr);
			if (token) {
				dfs_obj_t *sym;

				rc = dfs_lookup(dfs, obj->value, flags, &sym,
						NULL);
				if (rc) {
					D_DEBUG(DB_TRACE,
						"Failed to lookup symlink %s\n",
						obj->value);
					D_FREE(sym);
					D_GOTO(err_obj, rc);
				}

				parent.oh = sym->oh;
				D_FREE(sym);
				D_FREE(entry.value);
				obj->value = NULL;
				/*
				 * need to go to to the beginning of loop but we
				 * already did the strtok.
				 */
				goto dfs_lookup_loop;
			}

			/** return the symlink obj if this is the last entry */
			break;
		}

		rc = daos_obj_open(dfs->coh, entry.oid, daos_mode, &obj->oh,
				   NULL);
		if (rc) {
			D_ERROR("daos_obj_open() Failed (%d)\n", rc);
			D_GOTO(err_obj, rc = -daos_der2errno(rc));
		}

		oid_cp(&parent.oid, obj->oid);
		oid_cp(&parent.parent_oid, obj->parent_oid);
		parent.oh = obj->oh;
		parent.mode = entry.mode;
	}

	if (mode)
		*mode = obj->mode;
out:
	D_FREE(rem);
	*_obj = obj;
	return rc;
err_obj:
	D_FREE(obj);
	goto out;
}

int
dfs_nlinks(dfs_t *dfs, dfs_obj_t *obj, uint32_t *nlinks)
{
	if (dfs == NULL || !dfs->mounted)
		return -EINVAL;
	if (obj == NULL || !S_ISDIR(obj->mode))
		return -ENOTDIR;
	if (nlinks == NULL)
		return -EINVAL;

	return get_nlinks(obj->oh, DAOS_TX_NONE, nlinks, false);
}

int
dfs_readdir(dfs_t *dfs, dfs_obj_t *obj, daos_anchor_t *anchor, uint32_t *nr,
	    struct dirent *dirs)
{
	daos_key_desc_t	*kds;
	char		*enum_buf;
	uint32_t	number, key_nr, i;
	d_sg_list_t	sgl;
	int		rc;

	if (dfs == NULL || !dfs->mounted)
		return -EINVAL;
	if (obj == NULL || !S_ISDIR(obj->mode))
		return -ENOTDIR;
	if (*nr == 0)
		return 0;
	if (dirs == NULL || anchor == NULL)
		return -EINVAL;

	rc = check_access(dfs, geteuid(), getegid(), obj->mode, R_OK);
	if (rc)
		return rc;

	D_ALLOC_ARRAY(kds, *nr);
	if (kds == NULL)
		return -ENOMEM;

	D_ALLOC_ARRAY(enum_buf, *nr * DFS_MAX_PATH);
	if (enum_buf == NULL) {
		D_FREE(kds);
		return -ENOMEM;
	}

	key_nr = 0;
	number = *nr;
	while (!daos_anchor_is_eof(anchor)) {
		d_iov_t	iov;
		char	*ptr;

		memset(enum_buf, 0, (*nr) * DFS_MAX_PATH);

		sgl.sg_nr = 1;
		sgl.sg_nr_out = 0;
		d_iov_set(&iov, enum_buf, (*nr) * DFS_MAX_PATH);
		sgl.sg_iovs = &iov;

		rc = daos_obj_list_dkey(obj->oh, DAOS_TX_NONE, &number, kds,
					&sgl, anchor, NULL);
		if (rc)
			D_GOTO(out, rc = -daos_der2errno(rc));

		if (number == 0)
			continue; /* loop should break for EOF */

		for (ptr = enum_buf, i = 0; i < number; i++) {
			snprintf(dirs[key_nr].d_name, kds[i].kd_key_len + 1,
				 "%s", ptr);
			ptr += kds[i].kd_key_len;
			key_nr++;
		}
		number = *nr - key_nr;
		if (number == 0)
			break;
	}
	*nr = key_nr;

out:
	D_FREE(enum_buf);
	D_FREE(kds);
	return rc;
}

int
dfs_iterate(dfs_t *dfs, dfs_obj_t *obj, daos_anchor_t *anchor,
	    uint32_t *nr, size_t size, dfs_filler_cb_t op, void *udata)
{
	daos_key_desc_t	*kds;
	d_sg_list_t	sgl;
	d_iov_t		iov;
	uint32_t	num, keys_nr;
	char		*enum_buf;
	int		rc;

	if (dfs == NULL || !dfs->mounted)
		return -EINVAL;
	if (obj == NULL || !S_ISDIR(obj->mode))
		return -ENOTDIR;
	if (size == 0 || *nr == 0)
		return 0;
	if (anchor == NULL)
		return -EINVAL;

	rc = check_access(dfs, geteuid(), getegid(), obj->mode, R_OK);
	if (rc)
		return rc;

	num = *nr;
	D_ALLOC_ARRAY(kds, num);
	if (kds == NULL)
		return -ENOMEM;

	/** Allocate a buffer to store the entry keys */
	D_ALLOC_ARRAY(enum_buf, size);
	if (enum_buf == NULL) {
		D_FREE(kds);
		return -ENOMEM;
	}

	sgl.sg_nr = 1;
	sgl.sg_nr_out = 0;
	d_iov_set(&iov, enum_buf, size);
	sgl.sg_iovs = &iov;
	keys_nr = 0;

	while (!daos_anchor_is_eof(anchor)) {
		char		*ptr;
		uint32_t	i;

		/*
		 * list num or less entries, but not more than we can fit in
		 * enum_buf
		 */
		rc = daos_obj_list_dkey(obj->oh, DAOS_TX_NONE, &num, kds,
					&sgl, anchor, NULL);
		if (rc)
			D_GOTO(out, rc = -daos_der2errno(rc));

		/** If no entries were listed, continue to check anchor */
		if (num == 0)
			continue; /* loop should break for EOF */

		/** for every entry, issue the filler cb */
		for (ptr = enum_buf, i = 0; i < num; i++) {
			char name[DFS_MAX_PATH];

			snprintf(name, kds[i].kd_key_len + 1, "%s", ptr);
			if (op) {
				rc = op(dfs, obj, name, udata);
				if (rc)
					D_GOTO(out, rc);
			}

			/** advance pointer to next entry */
			ptr += kds[i].kd_key_len;
			/** adjust size of buffer data remaining */
			size -= kds[i].kd_key_len;
			keys_nr++;
		}
		num = *nr - keys_nr;
		/** stop if no more size or entries available to fill */
		if (size == 0 || num == 0)
			break;
		/** adjust iov for iteration */
		d_iov_set(&iov, ptr, size);
	}

	*nr = keys_nr;
out:
	D_FREE(kds);
	D_FREE(enum_buf);
	return rc;
}

int
dfs_lookup_rel(dfs_t *dfs, dfs_obj_t *parent, const char *name, int flags,
	       dfs_obj_t **_obj, mode_t *mode)
{
	dfs_obj_t		*obj;
	struct dfs_entry	entry = {0};
	bool			exists;
	int			daos_mode;
	int			rc = 0;

	if (dfs == NULL || !dfs->mounted)
		return -EINVAL;
	if (_obj == NULL)
		return -EINVAL;
	if (parent == NULL)
		parent = &dfs->root;
	else if (!S_ISDIR(parent->mode))
		return -ENOTDIR;

	rc = check_name(name);
	if (rc)
		return rc;
	rc = check_access(dfs, geteuid(), getegid(), parent->mode, X_OK);
	if (rc)
		return rc;
	daos_mode = get_daos_obj_mode(flags);
	if (daos_mode == -1)
		return -EINVAL;

	D_ALLOC_PTR(obj);
	if (obj == NULL)
		return -ENOMEM;

	rc = fetch_entry(parent->oh, DAOS_TX_NONE, name, true, &exists,
			 &entry);
	if (rc)
		D_GOTO(err_obj, rc);

	if (!exists)
		D_GOTO(err_obj, rc = -ENOENT);

	strncpy(obj->name, name, DFS_MAX_PATH);
	obj->name[DFS_MAX_PATH] = '\0';
	oid_cp(&obj->parent_oid, parent->oid);
	oid_cp(&obj->oid, entry.oid);
	obj->mode = entry.mode;

	/** if entry is a file, open the array object and return */
	if (S_ISREG(entry.mode)) {
		daos_size_t elem_size, chunk_size;

		rc = daos_array_open(dfs->coh, entry.oid, DAOS_TX_NONE,
				     daos_mode, &elem_size, &chunk_size,
				     &obj->oh, NULL);
		if (rc != 0) {
			D_ERROR("daos_array_open() failed (%d)\n", rc);
			D_GOTO(err_obj, rc = -daos_der2errno(rc));
		}
		if (elem_size != 1) {
			D_ERROR("Invalid Byte array elem size (%zu)\n",
				elem_size);
			daos_array_close(obj->oh, NULL);
			D_GOTO(err_obj, -EINVAL);
		}
	} else if (S_ISLNK(entry.mode)) {
		obj->value = entry.value;
	} else {
		rc = daos_obj_open(dfs->coh, entry.oid, daos_mode, &obj->oh,
				   NULL);
		if (rc) {
			D_ERROR("daos_obj_open() Failed (%d)\n", rc);
			D_GOTO(err_obj, rc = -daos_der2errno(rc));
		}
	}

	if (mode)
		*mode = obj->mode;
	*_obj = obj;

	return rc;
err_obj:
	D_FREE(obj);
	return rc;
}

int
dfs_open(dfs_t *dfs, dfs_obj_t *parent, const char *name, mode_t mode,
	 int flags, daos_oclass_id_t cid, daos_size_t chunk_size,
	 const char *value, dfs_obj_t **_obj)
{
	dfs_obj_t	*obj;
	daos_handle_t	th = DAOS_TX_NONE;
	int		rc;

	if (dfs == NULL || !dfs->mounted)
		return -EINVAL;
	if ((dfs->amode != O_RDWR) && (flags & O_CREAT))
		return -EPERM;
	if (_obj == NULL)
		return -EINVAL;
	if (S_ISLNK(mode) && value == NULL)
		return -EINVAL;
	if (parent == NULL)
		parent = &dfs->root;
	else if (!S_ISDIR(parent->mode))
		return -ENOTDIR;

	rc = check_name(name);
	if (rc)
		return rc;
	rc = check_access(dfs, geteuid(), getegid(), parent->mode,
			  (flags & O_CREAT) ? W_OK | X_OK : X_OK);
	if (rc)
		return rc;

	D_ALLOC_PTR(obj);
	if (obj == NULL)
		return -ENOMEM;

	strncpy(obj->name, name, DFS_MAX_PATH);
	obj->name[DFS_MAX_PATH] = '\0';
	obj->mode = mode;
	oid_cp(&obj->parent_oid, parent->oid);

	switch (mode & S_IFMT) {
	case S_IFREG:
		rc = open_file(dfs, th, parent, flags, cid, chunk_size, obj);
		if (rc) {
			D_DEBUG(DB_TRACE, "Failed to open file (%d)\n", rc);
			D_GOTO(out, rc);
		}
		break;
	case S_IFDIR:
		rc = open_dir(dfs, th, parent->oh, flags, cid, obj);
		if (rc) {
			D_DEBUG(DB_TRACE, "Failed to open dir (%d)\n", rc);
			D_GOTO(out, rc);
		}
		break;
	case S_IFLNK:
		rc = open_symlink(dfs, th, parent, flags, value, obj);
		if (rc) {
			D_DEBUG(DB_TRACE, "Failed to open symlink (%d)\n", rc);
			D_GOTO(out, rc);
		}
		break;
	default:
		D_ERROR("Invalid entry type (not a dir, file, symlink).\n");
		D_GOTO(out, rc = -EINVAL);
	}

	*_obj = obj;

	return rc;
out:
	D_FREE(obj);
	return rc;
}

int
dfs_dup(dfs_t *dfs, dfs_obj_t *obj, int flags, dfs_obj_t **_new_obj)
{
	dfs_obj_t	*new_obj;
	unsigned int	daos_mode;
	int		rc;

	if (dfs == NULL || !dfs->mounted)
		return -EINVAL;
	if (obj == NULL || _new_obj == NULL)
		return -EINVAL;

	daos_mode = get_daos_obj_mode(flags);
	if (daos_mode == -1)
		return -EINVAL;

	rc = check_access(dfs, geteuid(), getegid(), obj->mode,
			  (daos_mode == DAOS_OO_RO) ? R_OK : R_OK | W_OK);
	if (rc)
		return rc;

	D_ALLOC_PTR(new_obj);
	if (new_obj == NULL)
		return -ENOMEM;

	switch (obj->mode & S_IFMT) {
	case S_IFDIR:
		rc = daos_obj_open(dfs->coh, obj->oid, daos_mode,
				   &new_obj->oh, NULL);
		if (rc)
			D_GOTO(err, rc = -daos_der2errno(rc));
		break;
	case S_IFREG:
	{
		char		buf[1024];
		daos_iov_t	ghdl;

		daos_iov_set(&ghdl, buf, 1024);

		rc = daos_array_local2global(obj->oh, &ghdl);
		if (rc)
			D_GOTO(err, rc = -daos_der2errno(rc));

		rc = daos_array_global2local(dfs->coh, ghdl, daos_mode,
					     &new_obj->oh);
		if (rc)
			D_GOTO(err, rc = -daos_der2errno(rc));
		break;
	}
	case S_IFLNK:
		new_obj->value = strdup(obj->value);
		break;
	default:
		D_ERROR("Invalid object type (not a dir, file, symlink).\n");
		D_GOTO(err, rc = -EINVAL);
	}

	strncpy(new_obj->name, obj->name, DFS_MAX_PATH);
	new_obj->mode = obj->mode;
	oid_cp(&new_obj->parent_oid, obj->parent_oid);
	oid_cp(&new_obj->oid, obj->oid);

	*_new_obj = new_obj;
	return 0;

err:
	D_FREE(new_obj);
	return rc;
}

int
dfs_release(dfs_obj_t *obj)
{
	int rc = 0;

	if (obj == NULL)
		return -EINVAL;

	if (S_ISDIR(obj->mode))
		rc = daos_obj_close(obj->oh, NULL);
	else if (S_ISREG(obj->mode))
		rc = daos_array_close(obj->oh, NULL);
	else if (S_ISLNK(obj->mode))
		D_FREE(obj->value);
	else
		D_ASSERT(0);

	if (rc) {
		D_ERROR("daos_obj_close() Failed (%d)\n", rc);
		return -daos_der2errno(rc);
	}

	D_FREE(obj);
	return 0;
}

static int
io_internal(dfs_t *dfs, dfs_obj_t *obj, d_sg_list_t sgl, daos_off_t off,
	    int flag)
{
	daos_array_iod_t	iod;
	daos_range_t		rg;
	daos_size_t		buf_size;
	int			i;
	int			rc;

	buf_size = 0;
	for (i = 0; i < sgl.sg_nr; i++)
		buf_size += sgl.sg_iovs[i].iov_len;

	/** set array location */
	iod.arr_nr = 1;
	rg.rg_len = buf_size;
	rg.rg_idx = off;
	iod.arr_rgs = &rg;

	D_DEBUG(DB_TRACE, "IO OP %d, Off %"PRIu64", Len %zu\n",
		flag, off, buf_size);

	if (flag == DFS_WRITE) {
		rc = daos_array_write(obj->oh, DAOS_TX_NONE, &iod, &sgl,
				      NULL, NULL);
		if (rc)
			D_ERROR("daos_array_write() failed (%d)\n", rc);
	} else if (flag == DFS_READ) {
		rc = daos_array_read(obj->oh, DAOS_TX_NONE, &iod, &sgl, NULL,
				     NULL);
		if (rc)
			D_ERROR("daos_array_write() failed (%d)\n", rc);
	} else {
		rc = -DER_INVAL;
	}

	return -daos_der2errno(rc);
}

int
dfs_read(dfs_t *dfs, dfs_obj_t *obj, d_sg_list_t sgl, daos_off_t off,
	 daos_size_t *read_size)
{
	daos_size_t	array_size, max_read;
	daos_size_t	bytes_to_read, rem;
	int		i;
	int		rc;

	if (dfs == NULL || !dfs->mounted)
		return -EINVAL;
	if (obj == NULL || !S_ISREG(obj->mode))
		return -EINVAL;

	rc = daos_array_get_size(obj->oh, DAOS_TX_NONE, &array_size, NULL);
	if (rc) {
		D_ERROR("daos_array_get_size() failed (%d)\n", rc);
		return -daos_der2errno(rc);
	}

	if (off >= array_size) {
		*read_size = 0;
		return 0;
	}

	/* Update SGL in case we try to read beyond eof to not do that */
	max_read = array_size - off;
	bytes_to_read = 0;
	for (i = 0; i < sgl.sg_nr; i++) {
		if (bytes_to_read + sgl.sg_iovs[i].iov_len <= max_read) {
			bytes_to_read += sgl.sg_iovs[i].iov_len;
		} else {
			rem = max_read - bytes_to_read;
			if (rem) {
				bytes_to_read += rem;
				sgl.sg_iovs[i].iov_len = rem;
				i++;
				break;
			}
		}
	}
	sgl.sg_nr = i;

	rc = io_internal(dfs, obj, sgl, off, DFS_READ);
	if (rc) {
		D_ERROR("daos_array_read() failed (%d)\n", rc);
		return rc;
	}

	*read_size = bytes_to_read;
	return 0;
}

int
dfs_write(dfs_t *dfs, dfs_obj_t *obj, d_sg_list_t sgl, daos_off_t off)
{
	if (dfs == NULL || !dfs->mounted)
		return -EINVAL;
	if (dfs->amode != O_RDWR)
		return -EPERM;
	if (obj == NULL || !S_ISREG(obj->mode))
		return -EINVAL;

	return io_internal(dfs, obj, sgl, off, DFS_WRITE);
}

int
dfs_stat(dfs_t *dfs, dfs_obj_t *parent, const char *name, struct stat *stbuf)
{
	daos_handle_t	oh;
	int		rc;

	if (dfs == NULL || !dfs->mounted)
		return -EINVAL;
	if (parent == NULL)
		parent = &dfs->root;
	else if (!S_ISDIR(parent->mode))
		return -ENOTDIR;

	rc = check_access(dfs, geteuid(), getegid(), parent->mode, X_OK);
	if (rc)
		return rc;

	if (name == NULL) {
		if (strcmp(parent->name, "/") != 0) {
			D_ERROR("Invalid path %s and entry name %s)\n",
				parent->name, name);
			return -EINVAL;
		}
		name = parent->name;
		oh = dfs->super_oh;
	} else {
		rc = check_name(name);
		if (rc)
			return rc;
		oh = parent->oh;
	}

	return entry_stat(dfs, DAOS_TX_NONE, oh, name, stbuf);
}

int
dfs_ostat(dfs_t *dfs, dfs_obj_t *obj, struct stat *stbuf)
{
	daos_handle_t           oh;
	int			rc;

	if (dfs == NULL || !dfs->mounted)
		return -EINVAL;
	if (obj == NULL)
		return -EINVAL;

	/** Open parent object and fetch entry of obj from it */
	rc = daos_obj_open(dfs->coh, obj->parent_oid, DAOS_OO_RO, &oh, NULL);
	if (rc)
		return -daos_der2errno(rc);

	rc = entry_stat(dfs, DAOS_TX_NONE, oh, obj->name, stbuf);
	if (rc)
		D_GOTO(out, rc);

out:
	daos_obj_close(oh, NULL);
	return rc;
}

int
dfs_access(dfs_t *dfs, dfs_obj_t *parent, const char *name, int mask)
{
	daos_handle_t		oh;
	bool			exists;
	struct dfs_entry	entry;
	int			rc;

	if (dfs == NULL || !dfs->mounted)
		return -EINVAL;
	if (((mask & W_OK) == W_OK) && dfs->amode != O_RDWR)
		return -EPERM;
	if (parent == NULL)
		parent = &dfs->root;
	else if (!S_ISDIR(parent->mode))
		return -ENOTDIR;
	if (name == NULL) {
		if (strcmp(parent->name, "/") != 0) {
			D_ERROR("Invalid path %s and entry name %s\n",
				parent->name, name);
			return -EINVAL;
		}
		name = parent->name;
		oh = dfs->super_oh;
	} else {
		rc = check_name(name);
		if (rc)
			return rc;
		oh = parent->oh;
	}

	/* Check if parent has the entry */
	rc = fetch_entry(oh, DAOS_TX_NONE, name, true, &exists, &entry);
	if (rc)
		return rc;

	if (!exists)
		return -ENOENT;

	if (!S_ISLNK(entry.mode)) {
		if (mask == F_OK)
			return 0;

		/** Use real uid and gid for access() */
		return check_access(dfs, getuid(), getgid(), entry.mode, mask);
	}

	dfs_obj_t *sym;

	if (entry.value == NULL) {
		D_ERROR("Null Symlink value\n");
		return -EIO;
	}

	rc = dfs_lookup(dfs, entry.value, O_RDONLY, &sym, NULL);
	if (rc) {
		D_DEBUG(DB_TRACE, "Failed to lookup symlink %s\n", entry.value);
		return rc;
	}

	if (mask != F_OK)
		rc = check_access(dfs, getuid(), getgid(), sym->mode, mask);

	dfs_release(sym);
	return rc;
}

int
dfs_chmod(dfs_t *dfs, dfs_obj_t *parent, const char *name, mode_t mode)
{
	uid_t			euid;
	daos_handle_t		oh;
	daos_handle_t		th = DAOS_TX_NONE;
	bool			exists;
	struct dfs_entry	entry;
	d_sg_list_t		sgl;
	d_iov_t		sg_iov;
	daos_iod_t		iod;
	daos_key_t		dkey;
	int			rc;

	if (dfs == NULL || !dfs->mounted)
		return -EINVAL;
	if (dfs->amode != O_RDWR)
		return -EPERM;
	if (parent == NULL)
		parent = &dfs->root;
	else if (!S_ISDIR(parent->mode))
		return -ENOTDIR;
	if (name == NULL) {
		if (strcmp(parent->name, "/") != 0) {
			D_ERROR("Invalid path %s and entry name %s)\n",
				parent->name, name);
			return -EINVAL;
		}
		name = parent->name;
		oh = dfs->super_oh;
	} else {
		rc = check_name(name);
		if (rc)
			return rc;
		oh = parent->oh;
	}

	euid = geteuid();
	/** only root or owner can change mode */
	if (euid != 0 && dfs->uid != euid)
		return -EPERM;

	/** sticky bit, set-user-id and set-group-id, not supported yet */
	if (mode & S_ISVTX || mode & S_ISGID || mode & S_ISUID) {
		D_ERROR("setuid, setgid, & sticky bit are not supported.\n");
		return -EINVAL;
	}

	/* Check if parent has the entry */
	rc = fetch_entry(oh, DAOS_TX_NONE, name, true, &exists, &entry);
	if (rc)
		D_GOTO(out, rc);

	if (!exists)
		D_GOTO(out, rc = -ENOENT);

	/** resolve symlink */
	if (S_ISLNK(entry.mode)) {
		dfs_obj_t *sym;

		if (entry.value == NULL) {
			D_ERROR("Null Symlink value\n");
			return -EIO;
		}

		rc = dfs_lookup(dfs, entry.value, O_RDWR, &sym, NULL);
		if (rc) {
			D_ERROR("Failed to lookup Symlink %s\n", entry.value);
			return rc;
		}

		rc = daos_obj_open(dfs->coh, sym->parent_oid, DAOS_OO_RW,
				   &oh, NULL);
		dfs_release(sym);
		if (rc)
			return -daos_der2errno(rc);
	}

	/** set dkey as the entry name */
	d_iov_set(&dkey, (void *)name, strlen(name));

	/** set akey as the mode attr name */
	d_iov_set(&iod.iod_name, MODE_NAME, strlen(MODE_NAME));
	daos_csum_set(&iod.iod_kcsum, NULL, 0);
	iod.iod_nr	= 1;
	iod.iod_recxs	= NULL;
	iod.iod_eprs	= NULL;
	iod.iod_csums	= NULL;
	iod.iod_type	= DAOS_IOD_SINGLE;
	iod.iod_size	= sizeof(mode_t);

	/** set sgl for update */
	d_iov_set(&sg_iov, &mode, sizeof(mode_t));
	sgl.sg_nr	= 1;
	sgl.sg_nr_out	= 0;
	sgl.sg_iovs	= &sg_iov;

	rc = daos_obj_update(oh, th, &dkey, 1, &iod, &sgl, NULL);
	if (rc) {
		D_ERROR("Failed to update mode (rc = %d)\n", rc);
		D_GOTO(out, rc = -daos_der2errno(rc));
	}

	if (S_ISLNK(entry.mode))
		daos_obj_close(oh, NULL);

out:
	return rc;
}

int
dfs_get_size(dfs_t *dfs, dfs_obj_t *obj, daos_size_t *size)
{
	int rc;

	if (dfs == NULL || !dfs->mounted)
		return -EINVAL;
	if (obj == NULL || !S_ISREG(obj->mode))
		return -EINVAL;

	rc = check_access(dfs, geteuid(), getegid(), obj->mode, R_OK);
	if (rc)
		return rc;

	return daos_array_get_size(obj->oh, DAOS_TX_NONE, size, NULL);
}

int
dfs_punch(dfs_t *dfs, dfs_obj_t *obj, daos_off_t offset, daos_size_t len)
{
	daos_size_t		size;
	daos_array_iod_t	iod;
	daos_range_t		rg;
	int			rc;

	if (dfs == NULL || !dfs->mounted)
		return -EINVAL;
	if (dfs->amode != O_RDWR)
		return -EPERM;
	if (obj == NULL || !S_ISREG(obj->mode))
		return -EINVAL;

	rc = check_access(dfs, geteuid(), getegid(), obj->mode, W_OK);
	if (rc)
		return rc;

	/** simple truncate */
	if (len == DFS_MAX_FSIZE) {
		rc = daos_array_set_size(obj->oh, DAOS_TX_NONE, offset, NULL);
		return -daos_der2errno(rc);
	}

	rc = daos_array_get_size(obj->oh, DAOS_TX_NONE, &size, NULL);
	if (rc)
		return -daos_der2errno(rc);

	/** nothing to do if offset is the same as the file size */
	if (size == offset)
		return 0;

	/** if file is smaller than the offset, extend the file */
	if (size < offset) {
		rc = daos_array_set_size(obj->oh, DAOS_TX_NONE, offset, NULL);
		return -daos_der2errno(rc);
	}

	/** if fsize is between the range to punch, just truncate to offset */
	if (offset < size && size <= offset + len) {
		rc = daos_array_set_size(obj->oh, DAOS_TX_NONE, offset, NULL);
		return -daos_der2errno(rc);
	}

	D_ASSERT(size > offset + len);

	/** Punch offset -> len */
	iod.arr_nr = 1;
	rg.rg_len = offset + len;
	rg.rg_idx = offset;
	iod.arr_rgs = &rg;

	rc = daos_array_punch(obj->oh, DAOS_TX_NONE, &iod, NULL);
	if (rc) {
		D_ERROR("daos_array_punch() failed (%d)\n", rc);
		return -daos_der2errno(rc);
	}

	return rc;
}

int
dfs_get_mode(dfs_obj_t *obj, mode_t *mode)
{
	if (obj == NULL || mode == NULL)
		return -EINVAL;

	*mode = obj->mode;
	return 0;
}

int
dfs_get_symlink_value(dfs_obj_t *obj, char *buf, daos_size_t *size)
{
	daos_size_t val_size;

	if (obj == NULL || !S_ISLNK(obj->mode))
		return -EINVAL;

	val_size = strlen(obj->value);
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
dfs_move(dfs_t *dfs, dfs_obj_t *parent, char *name, dfs_obj_t *new_parent,
	 char *new_name)
{
	struct dfs_entry	entry = {0}, new_entry = {0};
	daos_handle_t		th = DAOS_TX_NONE;
	bool			exists;
	daos_key_t		dkey;
	int			rc;

	if (dfs == NULL || !dfs->mounted)
		return -EINVAL;
	if (dfs->amode != O_RDWR)
		return -EPERM;
	if (parent == NULL)
		parent = &dfs->root;
	else if (!S_ISDIR(parent->mode))
		return -ENOTDIR;
	if (new_parent == NULL)
		new_parent = &dfs->root;
	else if (!S_ISDIR(new_parent->mode))
		return -ENOTDIR;

	rc = check_name(name);
	if (rc)
		return rc;
	rc = check_name(new_name);
	if (rc)
		return rc;

	/*
	 * TODO - more permission checks for source & target attributes needed
	 * (immutable, append).
	 */

	rc = check_access(dfs, geteuid(), getegid(), parent->mode, W_OK | X_OK);
	if (rc)
		return rc;
	rc = check_access(dfs, geteuid(), getegid(), new_parent->mode,
			  W_OK | X_OK);
	if (rc)
		return rc;

	rc = fetch_entry(parent->oh, th, name, true, &exists, &entry);
	if (rc) {
		D_ERROR("Failed to fetch entry %s (%d)\n", name, rc);
		D_GOTO(out, rc);
	}
	if (exists == false)
		D_GOTO(out, rc);

	rc = fetch_entry(new_parent->oh, th, new_name, true, &exists,
			 &new_entry);
	if (rc) {
		D_ERROR("Failed to fetch entry %s (%d)\n", new_name, rc);
		D_GOTO(out, rc);
	}

	if (exists) {
		if (S_ISDIR(new_entry.mode)) {
			uint32_t nlinks = 0;
			daos_handle_t oh;

			/** if old entry not a dir, return error */
			if (!S_ISDIR(entry.mode)) {
				D_ERROR("Can't rename non dir over a dir\n");
				D_GOTO(out, rc = -EINVAL);
			}

			/** make sure new dir is empty */
			rc = daos_obj_open(dfs->coh, new_entry.oid, DAOS_OO_RW,
					   &oh, NULL);
			if (rc) {
				D_ERROR("daos_obj_open() Failed (%d)\n", rc);
				D_GOTO(out, rc = -daos_der2errno(rc));
			}

			rc = get_nlinks(oh, th, &nlinks, true);
			if (rc) {
				D_ERROR("failed to check dir %s (%d)\n",
					new_name, rc);
				daos_obj_close(oh, NULL);
				D_GOTO(out, rc);
			}

			rc = daos_obj_close(oh, NULL);
			if (rc) {
				D_ERROR("daos_obj_close() Failed (%d)\n", rc);
				D_GOTO(out, rc = -daos_der2errno(rc));
			}

			if (nlinks != 0) {
				D_ERROR("target dir is not empty\n");
				D_GOTO(out, rc = -ENOTEMPTY);
			}
		}

		rc = remove_entry(dfs, th, new_parent->oh, new_name, new_entry);
		if (rc) {
			D_ERROR("Failed to remove entry %s (%d)\n",
				new_name, rc);
			D_GOTO(out, rc);
		}
	}

	/** rename symlink */
	if (S_ISLNK(entry.mode)) {
		rc = remove_entry(dfs, th, parent->oh, name, entry);
		if (rc) {
			D_ERROR("Failed to remove entry %s (%d)\n",
				name, rc);
			D_GOTO(out, rc);
		}

		rc = insert_entry(parent->oh, th, new_name, entry);
		if (rc)
			D_ERROR("Inserting new entry %s failed (%d)\n",
				new_name, rc);
		D_GOTO(out, rc);
	}

	entry.atime = entry.mtime = entry.ctime = time(NULL);
	/** insert old entry in new parent object */
	rc = insert_entry(new_parent->oh, th, new_name, entry);
	if (rc) {
		D_ERROR("Inserting entry %s failed (%d)\n", new_name, rc);
		D_GOTO(out, rc);
	}

	/** remove the old entry from the old parent (just the dkey) */
	d_iov_set(&dkey, (void *)name, strlen(name));
	rc = daos_obj_punch_dkeys(parent->oh, th, 1, &dkey, NULL);
	if (rc) {
		D_ERROR("Punch entry %s failed (%d)\n", name, rc);
		D_GOTO(out, rc = -daos_der2errno(rc));
	}

out:
	if (entry.value) {
		D_ASSERT(S_ISLNK(entry.mode));
		D_FREE(entry.value);
	}
	if (new_entry.value) {
		D_ASSERT(S_ISLNK(new_entry.mode));
		D_FREE(new_entry.value);
	}
	return rc;
}

int
dfs_exchange(dfs_t *dfs, dfs_obj_t *parent1, char *name1, dfs_obj_t *parent2,
	     char *name2)
{
	struct dfs_entry	entry1 = {0}, entry2 = {0};
	daos_handle_t		th = DAOS_TX_NONE;
	bool			exists;
	daos_key_t		dkey;
	int			rc;

	if (dfs == NULL || !dfs->mounted)
		return -EINVAL;
	if (dfs->amode != O_RDWR)
		return -EPERM;
	if (parent1 == NULL)
		parent1 = &dfs->root;
	else if (!S_ISDIR(parent1->mode))
		return -ENOTDIR;
	if (parent2 == NULL)
		parent2 = &dfs->root;
	else if (!S_ISDIR(parent2->mode))
		return -ENOTDIR;

	rc = check_name(name1);
	if (rc)
		return rc;
	rc = check_name(name2);
	if (rc)
		return rc;
	rc = check_access(dfs, geteuid(), getegid(), parent1->mode,
			  W_OK | X_OK);
	if (rc)
		return rc;
	rc = check_access(dfs, geteuid(), getegid(), parent2->mode,
			  W_OK | X_OK);
	if (rc)
		return rc;

	rc = fetch_entry(parent1->oh, th, name1, true, &exists, &entry1);
	if (rc) {
		D_ERROR("Failed to fetch entry %s (%d)\n", name1, rc);
		D_GOTO(out, rc);
	}
	if (exists == false)
		D_GOTO(out, rc = -EINVAL);

	rc = fetch_entry(parent2->oh, th, name2, true, &exists, &entry2);
	if (rc) {
		D_ERROR("Failed to fetch entry %s (%d)\n", name2, rc);
		D_GOTO(out, rc);
	}

	if (exists == false)
		D_GOTO(out, rc = -EINVAL);

	/** remove the first entry from parent1 (just the dkey) */
	d_iov_set(&dkey, (void *)name1, strlen(name1));
	rc = daos_obj_punch_dkeys(parent1->oh, th, 1, &dkey, NULL);
	if (rc) {
		D_ERROR("Punch entry %s failed (%d)\n", name1, rc);
		D_GOTO(out, rc = -daos_der2errno(rc));
	}

	/** remove the second entry from parent2 (just the dkey) */
	d_iov_set(&dkey, (void *)name2, strlen(name2));
	rc = daos_obj_punch_dkeys(parent2->oh, th, 1, &dkey, NULL);
	if (rc) {
		D_ERROR("Punch entry %s failed (%d)\n", name2, rc);
		D_GOTO(out, rc = -daos_der2errno(rc));
	}

	entry1.atime = entry1.mtime = entry1.ctime = time(NULL);
	/** insert entry1 in parent2 object */
	rc = insert_entry(parent2->oh, th, name1, entry1);
	if (rc) {
		D_ERROR("Inserting entry %s failed (%d)\n", name1, rc);
		D_GOTO(out, rc);
	}

	entry2.atime = entry2.mtime = entry2.ctime = time(NULL);
	/** insert entry2 in parent1 object */
	rc = insert_entry(parent1->oh, th, name2, entry2);
	if (rc) {
		D_ERROR("Inserting entry %s failed (%d)\n", name2, rc);
		D_GOTO(out, rc);
	}

out:
	if (entry1.value) {
		D_ASSERT(S_ISLNK(entry1.mode));
		D_FREE(entry1.value);
	}
	if (entry2.value) {
		D_ASSERT(S_ISLNK(entry2.mode));
		D_FREE(entry2.value);
	}
	return rc;
}

int
dfs_sync(dfs_t *dfs)
{
	if (dfs == NULL || !dfs->mounted)
		return -EINVAL;
	if (dfs->amode != O_RDWR)
		return -EPERM;

	/** Take a snapshot here and allow rollover to that when supported. */

	return 0;
}

static char *
concat(const char *s1, const char *s2)
{
	char *result = NULL;

	result = malloc(strlen(s1)+strlen(s2)+1);
	if (result == NULL)
		return NULL;

	strcpy(result, s1);
	strcat(result, s2);

	return result;
}

int
dfs_setxattr(dfs_t *dfs, dfs_obj_t *obj, const char *name,
	     const void *value, daos_size_t size, int flags)
{
	char		*xname = NULL;
	daos_handle_t	th = DAOS_TX_NONE;
	d_sg_list_t	sgl;
	d_iov_t	sg_iov;
	daos_iod_t	iod;
	daos_key_t	dkey;
	daos_handle_t	oh;
	int		rc;

	if (dfs == NULL || !dfs->mounted)
		return -EINVAL;
	if (dfs->amode != O_RDWR)
		return -EPERM;
	if (obj == NULL)
		return -EINVAL;

	rc = check_access(dfs, geteuid(), getegid(), obj->mode, W_OK);
	if (rc)
		return rc;

	/** prefix name with x: to avoid collision with internal attrs */
	xname = concat("x:", name);
	if (xname == NULL)
		return -ENOMEM;

	/** Open parent object and insert xattr in the entry of the object */
	rc = daos_obj_open(dfs->coh, obj->parent_oid, DAOS_OO_RW, &oh, NULL);
	if (rc)
		D_GOTO(out, rc = -daos_der2errno(rc));

	/** set dkey as the entry name */
	d_iov_set(&dkey, (void *)obj->name, strlen(obj->name));

	/** set akey as the xattr name */
	d_iov_set(&iod.iod_name, xname, strlen(xname));
	daos_csum_set(&iod.iod_kcsum, NULL, 0);
	iod.iod_nr	= 1;
	iod.iod_recxs	= NULL;
	iod.iod_eprs	= NULL;
	iod.iod_csums	= NULL;
	iod.iod_type	= DAOS_IOD_SINGLE;

	/** if not default flag, check for xattr existence */
	if (flags != 0) {
		bool exists;

		iod.iod_size	= DAOS_REC_ANY;
		rc = daos_obj_fetch(oh, th, &dkey, 1, &iod, NULL, NULL, NULL);
		if (rc)
			D_GOTO(out, rc = -daos_der2errno(rc));

		if (iod.iod_size == 0)
			exists = false;
		else
			exists = true;

		if (flags == XATTR_CREATE && exists) {
			D_ERROR("Xattribute already exists (XATTR_CREATE)");
			D_GOTO(out, rc = -EEXIST);
		} else if (flags == XATTR_REPLACE && !exists) {
			D_ERROR("Xattribute does not exist (XATTR_REPLACE)");
			D_GOTO(out, rc = -ENOENT);
		}
	}

	/** set sgl for update */
	d_iov_set(&sg_iov, (void *)value, size);
	sgl.sg_nr	= 1;
	sgl.sg_nr_out	= 0;
	sgl.sg_iovs	= &sg_iov;

	iod.iod_size	= size;
	rc = daos_obj_update(oh, th, &dkey, 1, &iod, &sgl, NULL);
	if (rc) {
		D_ERROR("Failed to add extended attribute %s\n", name);
		D_GOTO(out, rc = -daos_der2errno(rc));
	}

out:
	if (xname)
		D_FREE(xname);
	daos_obj_close(oh, NULL);
	return rc;
}

int
dfs_getxattr(dfs_t *dfs, dfs_obj_t *obj, const char *name, void *value,
	     daos_size_t *size)
{
	char            *xname = NULL;
	d_sg_list_t	sgl;
	d_iov_t	sg_iov;
	daos_iod_t	iod;
	daos_key_t	dkey;
	daos_handle_t	oh;
	int		rc;

	if (dfs == NULL || !dfs->mounted)
		return -EINVAL;
	if (obj == NULL)
		return -EINVAL;

	rc = check_access(dfs, geteuid(), getegid(), obj->mode, R_OK);
	if (rc)
		return rc;

	xname = concat("x:", name);
	if (xname == NULL)
		return -ENOMEM;

	/** Open parent object and get xattr from the entry of the object */
	rc = daos_obj_open(dfs->coh, obj->parent_oid, DAOS_OO_RO, &oh, NULL);
	if (rc)
		return -daos_der2errno(rc);

	/** set dkey as the entry name */
	d_iov_set(&dkey, (void *)obj->name, strlen(obj->name));

	/** set akey as the xattr name */
	d_iov_set(&iod.iod_name, xname, strlen(xname));
	daos_csum_set(&iod.iod_kcsum, NULL, 0);
	iod.iod_nr	= 1;
	iod.iod_recxs	= NULL;
	iod.iod_eprs	= NULL;
	iod.iod_csums	= NULL;
	iod.iod_type	= DAOS_IOD_SINGLE;

	if (*size) {
		iod.iod_size	= *size;

		/** set sgl for fetch */
		d_iov_set(&sg_iov, value, *size);
		sgl.sg_nr	= 1;
		sgl.sg_nr_out	= 0;
		sgl.sg_iovs	= &sg_iov;

		rc = daos_obj_fetch(oh, DAOS_TX_NONE, &dkey, 1, &iod, &sgl,
				    NULL, NULL);
	} else {
		iod.iod_size	= DAOS_REC_ANY;

		rc = daos_obj_fetch(oh, DAOS_TX_NONE, &dkey, 1, &iod, NULL,
				    NULL, NULL);
	}
	if (rc) {
		D_ERROR("Failed to fetch xattr %s (%d)\n", name, rc);
		D_GOTO(out, rc = -daos_der2errno(rc));
	}

	*size = iod.iod_size;
	if (iod.iod_size == 0)
		D_GOTO(out, rc = -ENOENT);

out:
	if (xname)
		D_FREE(xname);
	daos_obj_close(oh, NULL);
	return rc;
}

int
dfs_removexattr(dfs_t *dfs, dfs_obj_t *obj, const char *name)
{
	char            *xname = NULL;
	daos_handle_t	th = DAOS_TX_NONE;
	daos_iod_t	iod;
	daos_key_t	dkey;
	daos_handle_t	oh;
	int		rc;

	if (dfs == NULL || !dfs->mounted)
		return -EINVAL;
	if (dfs->amode != O_RDWR)
		return -EPERM;
	if (obj == NULL)
		return -EINVAL;

	rc = check_access(dfs, geteuid(), getegid(), obj->mode, W_OK);
	if (rc)
		return rc;

	xname = concat("x:", name);
	if (xname == NULL)
		return -ENOMEM;

	/** Open parent object and remove xattr from the entry of the object */
	rc = daos_obj_open(dfs->coh, obj->parent_oid, DAOS_OO_RW, &oh, NULL);
	if (rc)
		D_GOTO(out, rc = -daos_der2errno(rc));

	/** set dkey as the entry name */
	d_iov_set(&dkey, (void *)obj->name, strlen(obj->name));

	/** set akey as the xattr name */
	d_iov_set(&iod.iod_name, xname, strlen(xname));
	daos_csum_set(&iod.iod_kcsum, NULL, 0);
	iod.iod_nr	= 1;
	iod.iod_recxs	= NULL;
	iod.iod_eprs	= NULL;
	iod.iod_csums	= NULL;
	iod.iod_type	= DAOS_IOD_SINGLE;
	iod.iod_size	= 0;

	/** Punch the xattr */
	rc = daos_obj_update(oh, th, &dkey, 1, &iod, NULL, NULL);
	if (rc) {
		D_ERROR("Failed to punch extended attribute %s\n", name);
		D_GOTO(out, rc = -daos_der2errno(rc));
	}

out:
	if (xname)
		D_FREE(xname);
	daos_obj_close(oh, NULL);
	return rc;
}

int
dfs_listxattr(dfs_t *dfs, dfs_obj_t *obj, char *list, daos_size_t *size)
{
	daos_key_t	dkey;
	daos_handle_t	oh;
	daos_key_desc_t	kds[ENUM_DESC_NR];
	daos_anchor_t	anchor = {0};
	daos_size_t	list_size, ret_size;
	char		*ptr_list;
	int		rc;

	if (dfs == NULL || !dfs->mounted)
		return -EINVAL;
	if (obj == NULL)
		return -EINVAL;

	/** Open parent object and list from entry */
	rc = daos_obj_open(dfs->coh, obj->parent_oid, DAOS_OO_RW, &oh, NULL);
	if (rc)
		return -daos_der2errno(rc);

	/** set dkey as the entry name */
	d_iov_set(&dkey, (void *)obj->name, strlen(obj->name));

	list_size = *size;
	ret_size = 0;
	ptr_list = list;

	while (!daos_anchor_is_eof(&anchor)) {
		uint32_t	number = ENUM_DESC_NR;
		uint32_t	i;
		d_iov_t	iov;
		char		enum_buf[ENUM_DESC_BUF] = {0};
		d_sg_list_t	sgl;
		char		*ptr;

		sgl.sg_nr = 1;
		sgl.sg_nr_out = 0;
		d_iov_set(&iov, enum_buf, ENUM_DESC_BUF);
		sgl.sg_iovs = &iov;

		rc = daos_obj_list_akey(oh, DAOS_TX_NONE, &dkey, &number, kds,
					&sgl, &anchor, NULL);
		if (rc)
			D_GOTO(out, rc = -daos_der2errno(rc));

		if (number == 0)
			continue;

		for (ptr = enum_buf, i = 0; i < number; i++) {
			if (strncmp("x:", ptr, 2) != 0) {
				ptr += kds[i].kd_key_len;
				continue;
			}

			ret_size += kds[i].kd_key_len - 1;

			if (list == NULL)
				continue;
			if (list_size < kds[i].kd_key_len - 2)
				continue;

			snprintf(ptr_list, kds[i].kd_key_len - 1, "%s",
				 ptr + 2);

			list_size -= kds[i].kd_key_len - 1;
			ptr_list += kds[i].kd_key_len - 1;
			ptr += kds[i].kd_key_len;
		}
	}

	*size = ret_size;
out:
	daos_obj_close(oh, NULL);
	return rc;
}

int
dfs_obj2id(dfs_obj_t *obj, daos_obj_id_t *oid)
{
	if (oid == NULL)
		return -EINVAL;
	oid_cp(oid, obj->oid);
	return 0;
}

#define DFS_ROOT_UUID "ffffffff-ffff-ffff-ffff-ffffffffffff"

int
dfs_mount_root_cont(daos_handle_t poh, dfs_t **dfs)
{
	uuid_t			co_uuid;
	daos_cont_info_t	co_info;
	daos_handle_t		coh;
	bool			cont_created = false;
	int			rc;

	/** Use special UUID for root container */
	rc = uuid_parse(DFS_ROOT_UUID, co_uuid);
	if (rc) {
		D_ERROR("Invalid Container uuid\n");
		return rc;
	}

	/** Try to open the DAOS container first (the mountpoint) */
	rc = daos_cont_open(poh, co_uuid, DAOS_COO_RW, &coh, &co_info, NULL);
	/* If NOEXIST we create it */
	if (rc == -DER_NONEXIST) {
		rc = daos_cont_create(poh, co_uuid, NULL, NULL);
		if (rc == 0) {
			cont_created = true;
			rc = daos_cont_open(poh, co_uuid, DAOS_COO_RW, &coh,
					    &co_info, NULL);
		}
	}
	if (rc) {
		fprintf(stderr, "Failed to create/open container (%d)\n", rc);
		D_GOTO(out_del, rc = -daos_der2errno(rc));
	}

	rc = dfs_mount(poh, coh, O_RDWR, dfs);
	if (rc) {
		fprintf(stderr, "dfs_mount failed (%d)\n", rc);
		D_GOTO(out_cont, rc);
	}

	return 0;

out_cont:
	daos_cont_close(coh, NULL);
out_del:
	if (cont_created)
		daos_cont_destroy(poh, co_uuid, 1, NULL);
	return rc;
}

int
dfs_umount_root_cont(dfs_t *dfs)
{
	daos_handle_t	coh;
	int		rc;

	if (dfs == NULL)
		return -EINVAL;

	coh.cookie = dfs->coh.cookie;

	rc = dfs_umount(dfs);
	if (rc)
		return rc;

	rc = daos_cont_close(coh, NULL);
	return -daos_der2errno(rc);
}

/*  LocalWords:  EINVAL
 */
