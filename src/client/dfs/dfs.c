/**
 * (C) Copyright 2018-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC	DD_FAC(dfs)

#include <fcntl.h>
#include <math.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <linux/xattr.h>
#include <daos/checksum.h>
#include <daos/common.h>
#include <daos/event.h>
#include <daos/pool.h>
#include <daos/container.h>
#include <daos/cont_props.h>
#include <daos/array.h>
#include <daos/object.h>
#include <daos/placement.h>

#include "daos.h"
#include "daos_fs.h"

#include "dfs_internal.h"

/** D-key name of SB metadata */
#define SB_DKEY		"DFS_SB_METADATA"

#define SB_AKEYS	9
/** A-key name of SB magic */
#define MAGIC_NAME	"DFS_MAGIC"
/** A-key name of SB version */
#define SB_VER_NAME	"DFS_SB_VERSION"
/** A-key name of DFS Layout Version */
#define LAYOUT_VER_NAME	"DFS_LAYOUT_VERSION"
/** A-key name of Default chunk size */
#define CS_NAME		"DFS_CHUNK_SIZE"
/** A-key name of Default Object Class for objects */
#define OC_NAME		"DFS_OBJ_CLASS"
/** A-key name of Default Object Class for directories */
#define DIR_OC_NAME	"DFS_DIR_OBJ_CLASS"
/** A-key name of Default Object Class for files */
#define FILE_OC_NAME	"DFS_FILE_OBJ_CLASS"
/** Consistency mode of the DFS container */
#define CONT_MODE_NAME	"DFS_MODE"
/** A-key name of the object class hints */
#define CONT_HINT_NAME	"DFS_HINTS"

#define MAGIC_IDX	0
#define SB_VER_IDX	1
#define LAYOUT_VER_IDX	2
#define CS_IDX		3
#define OC_IDX		4
#define DIR_OC_IDX	5
#define FILE_OC_IDX	6
#define CONT_MODE_IDX	7
#define CONT_HINT_IDX	8

/** Magic Value */
#define DFS_SB_MAGIC		0xda05df50da05df50
/** DFS SB version value */
#define DFS_SB_VERSION		2
/** DFS Layout Version Value */
#define DFS_LAYOUT_VERSION	3
/** Magic value for serializing / deserializing a DFS handle */
#define DFS_GLOB_MAGIC		0xda05df50
/** Magic value for serializing / deserializing a DFS object handle */
#define DFS_OBJ_GLOB_MAGIC	0xdf500b90

/** Number of A-keys for attributes in any object entry */
#define INODE_AKEYS	12
#define INODE_AKEY_NAME	"DFS_INODE"
#define SLINK_AKEY_NAME	"DFS_SLINK"
#define MODE_IDX	0
#define OID_IDX		(sizeof(mode_t))
#define MTIME_IDX	(OID_IDX + sizeof(daos_obj_id_t))
#define CTIME_IDX	(MTIME_IDX + sizeof(uint64_t))
#define CSIZE_IDX	(CTIME_IDX + sizeof(uint64_t))
#define OCLASS_IDX	(CSIZE_IDX + sizeof(daos_size_t))
#define MTIME_NSEC_IDX	(OCLASS_IDX + sizeof(daos_oclass_id_t))
#define CTIME_NSEC_IDX	(MTIME_NSEC_IDX + sizeof(uint64_t))
#define UID_IDX		(CTIME_NSEC_IDX + sizeof(uint64_t))
#define GID_IDX		(UID_IDX + sizeof(uid_t))
#define SIZE_IDX	(GID_IDX + sizeof(gid_t))
#define HLC_IDX		(SIZE_IDX + sizeof(daos_size_t))
#define END_IDX		(HLC_IDX + sizeof(uint64_t))

/*
 * END IDX for layout V2 (2.0) is at the current offset where we store the mtime nsec, but also need
 * to account for the atime which is not stored anymore, but was stored (as a time_t) in layout V2.
 */
#define END_L2_IDX	(MTIME_NSEC_IDX + sizeof(time_t))

/** Parameters for dkey enumeration */
#define ENUM_DESC_NR	10
#define ENUM_DESC_BUF	(ENUM_DESC_NR * DFS_MAX_NAME)
#define ENUM_XDESC_BUF	(ENUM_DESC_NR * (DFS_MAX_XATTR_NAME + 2))

/** OIDs for Superblock and Root objects */
#define RESERVED_LO	0
#define SB_HI		0
#define ROOT_HI		1

/** DFS mode mask (3rd bit) */
#define MODE_MASK	(1 << 2)

/** Max recursion depth for symlinks */
#define DFS_MAX_RECURSION 40

typedef uint64_t dfs_magic_t;
typedef uint16_t dfs_sb_ver_t;
typedef uint16_t dfs_layout_ver_t;

/** object struct that is instantiated for a DFS open object */
struct dfs_obj {
	/** DAOS object ID */
	daos_obj_id_t		oid;
	/** DAOS object open handle */
	daos_handle_t		oh;
	/** mode_t containing permissions & type */
	mode_t			mode;
	/** open access flags */
	int			flags;
	/** DAOS object ID of the parent of the object */
	daos_obj_id_t		parent_oid;
	/** entry name of the object in the parent */
	char			name[DFS_MAX_NAME + 1];
	union {
		/** Symlink value if object is a symbolic link */
		char	*value;
		struct {
			/** Default object class for all entries in dir */
			daos_oclass_id_t        oclass;
			/** Default chunk size for all entries in dir */
			daos_size_t             chunk_size;
		} d;
	};
};

enum {
	DFS_NONE = 0,
	DFS_MOUNT,
	DFS_MOUNT_ALL,
};

/** dfs struct that is instantiated for a mounted DFS namespace */
struct dfs {
	/** flag to indicate whether the dfs is mounted */
	int			mounted;
	/** flag to indicate whether dfs is mounted with balanced mode (DTX) */
	bool			use_dtx;
	/** lock for threadsafety */
	pthread_mutex_t		lock;
	/** layout version of DFS container that is mounted */
	dfs_layout_ver_t	layout_v;
	/** uid - inherited from container. */
	uid_t			uid;
	/** gid - inherited from container. */
	gid_t			gid;
	/** Access mode (RDONLY, RDWR) */
	int			amode;
	/** Open pool handle of the DFS mount */
	daos_handle_t		poh;
	/** refcount on pool handle that through the DFS API */
	uint32_t		poh_refcount;
	/** Open container handle of the DFS mount */
	daos_handle_t		coh;
	/** refcount on cont handle that through the DFS API */
	uint32_t		coh_refcount;
	/** Object ID reserved for this DFS (see oid_gen below) */
	daos_obj_id_t		oid;
	/** superblock object OID */
	daos_obj_id_t		super_oid;
	/** Open object handle of SB */
	daos_handle_t		super_oh;
	/** Root object info */
	dfs_obj_t		root;
	/** DFS container attributes (Default chunk size, oclass, etc.) */
	dfs_attr_t		attr;
	/** Object class hint for files */
	daos_oclass_hints_t	file_oclass_hint;
	/** Object class hint for dirs */
	daos_oclass_hints_t	dir_oclass_hint;
	/** Optional prefix to account for when resolving an absolute path */
	char			*prefix;
	daos_size_t		prefix_len;
	/** hash entry for pool open handle - valid on dfs_connect() */
	struct dfs_mnt_hdls	*pool_hdl;
	/** hash entry for cont open handle - valid on dfs_connect() */
	struct dfs_mnt_hdls	*cont_hdl;
	/** the root dir stat buf */
	struct stat		root_stbuf;
};

struct dfs_entry {
	/** mode (permissions + entry type) */
	mode_t			mode;
	/* Length of value string, not including NULL byte */
	daos_size_t		value_len;
	/** Object ID if not a symbolic link */
	daos_obj_id_t		oid;
	/* Time of last modification (sec) */
	uint64_t		mtime;
	/* Time of last modification (nsec) */
	uint64_t		mtime_nano;
	/* for regular file, the time of last modification of the object */
	uint64_t		obj_hlc;
	/* Time of last status change (sec) */
	uint64_t		ctime;
	/* Time of last status change (nsec) */
	uint64_t		ctime_nano;
	/** chunk size of file or default for all files in a dir */
	daos_size_t		chunk_size;
	/** oclass of file or all files in a dir */
	daos_oclass_id_t	oclass;
	/** uid - not enforced at this level. */
	uid_t			uid;
	/** gid - not enforced at this level. */
	gid_t			gid;
	/** Sym Link value */
	char			*value;
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

static inline bool
tspec_gt(struct timespec l, struct timespec r)
{
	if (l.tv_sec == r.tv_sec)
		return l.tv_nsec > r.tv_nsec;
	else
		return l.tv_sec > r.tv_sec;
}

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

static inline int
check_tx(daos_handle_t th, int rc)
{
	/** if we are not using a DTX, no restart is possible */
	if (daos_handle_is_valid(th)) {
		int ret;

		if (rc == ERESTART) {
			/** restart the TX handle */
			rc = daos_tx_restart(th, NULL);
			if (rc) {
				/** restart failed, so just fail */
				D_ERROR("daos_tx_restart() failed (%d)\n", rc);
				rc = daos_der2errno(rc);
			} else {
				/** restart succeeded, so return restart code */
				return ERESTART;
			}
		}

		/** on success or non-restart errors, close the handle */
		ret = daos_tx_close(th,  NULL);
		if (ret) {
			D_ERROR("daos_tx_close() failed (%d)\n", ret);
			if (rc == 0)
				rc = daos_der2errno(ret);
		}
	}

	return rc;
}

static int
decode_one_hint(char *hint, uint32_t rf, daos_oclass_hints_t *obj_hint, enum daos_otype_t *type)
{
	char *name;
	char *val;
	char *save = NULL;

	/** get object type (file or dir) */
	name = strtok_r(hint, ":", &save);
	if (name == NULL) {
		D_ERROR("Invalid object type in hint: %s\n", hint);
		return EINVAL;
	}
	if (strcasecmp(name, "dir") == 0 || strcasecmp(name, "directory") == 0) {
		*type = DAOS_OT_MULTI_HASHED;
		/** get hint value */
		val = strtok_r(NULL, "", &save);
		if (val == NULL) {
			D_ERROR("Invalid Hint value for directory type (%s)\n", hint);
			return EINVAL;
		}
		if (strcasecmp(val, "single") == 0) {
			if (rf == 0)
				*obj_hint = DAOS_OCH_SHD_TINY;
			else
				*obj_hint = DAOS_OCH_SHD_TINY | DAOS_OCH_RDD_RP;
		} else if (strcasecmp(val, "max") == 0) {
			if (rf == 0)
				*obj_hint = DAOS_OCH_SHD_MAX;
			else
				*obj_hint = DAOS_OCH_SHD_MAX | DAOS_OCH_RDD_RP;
		} else {
			D_ERROR("Invalid directory hint: %s\n", val);
			return EINVAL;
		}
	} else if (strcasecmp(name, "file") == 0) {
		*type = DAOS_OT_ARRAY_BYTE;
		/** get hint value */
		val = strtok_r(NULL, "", &save);
		if (val == NULL) {
			D_ERROR("Invalid Hint value for file type (%s)\n", hint);
			return EINVAL;
		}
		if (strcasecmp(val, "single") == 0) {
			if (rf == 0)
				*obj_hint = DAOS_OCH_SHD_TINY;
			else
				*obj_hint = DAOS_OCH_SHD_TINY | DAOS_OCH_RDD_RP;
		} else if (strcasecmp(val, "max") == 0) {
			if (rf == 0)
				*obj_hint = DAOS_OCH_SHD_MAX;
			else
				*obj_hint = DAOS_OCH_SHD_MAX | DAOS_OCH_RDD_EC;
		} else {
			D_ERROR("Invalid file hint: %s\n", val);
			return EINVAL;
		}
	} else {
		D_ERROR("Invalid object type in hint: %s\n", name);
		return EINVAL;
	}

	return 0;
}

static int
get_oclass_hints(const char *hints, daos_oclass_hints_t *dir_hints, daos_oclass_hints_t *file_hints,
		 uint64_t rf)
{
	char	*end_token = NULL;
	char	*t;
	char	*local;
	int	rc = 0;

	D_ASSERT(hints);
	*dir_hints = *file_hints = 0;
	D_STRNDUP(local, hints, DAOS_CONT_HINT_MAX_LEN);
	if (!local)
		return ENOMEM;

	/** get a hint value pair */
	t = strtok_r(local, ",", &end_token);
	if (t == NULL) {
		D_ERROR("Invalid hint format: %s\n", hints);
		D_GOTO(out, rc = EINVAL);
	}

	while (t != NULL) {
		daos_oclass_hints_t	obj_hint = 0;
		enum daos_otype_t	type;

		rc = decode_one_hint(t, rf, &obj_hint, &type);
		if (rc)
			D_GOTO(out, rc);

		if (type == DAOS_OT_ARRAY_BYTE)
			*file_hints = obj_hint;
		else
			*dir_hints = obj_hint;
		t = strtok_r(NULL, ",", &end_token);
	}

out:
	D_FREE(local);
	return rc;
}

int
dfs_suggest_oclass(dfs_t *dfs, const char *hint, daos_oclass_id_t *cid)
{
	daos_oclass_hints_t	obj_hint = 0;
	char			*local;
	uint32_t		rf;
	enum daos_otype_t	type;
	int			rc;

	if (dfs == NULL || !dfs->mounted)
		return EINVAL;
	if (hint == NULL)
		return EINVAL;
	if (strnlen(hint, DAOS_CONT_HINT_MAX_LEN) > DAOS_CONT_HINT_MAX_LEN + 1)
		return EINVAL;

	/** get the Redundancy Factor */
	rc = dc_cont_hdl2redunfac(dfs->coh, &rf);
	if (rc) {
		D_ERROR("dc_cont_hdl2redunfac() failed "DF_RC"\n", DP_RC(rc));
		return daos_der2errno(rc);
	}

	D_STRNDUP(local, hint, DAOS_CONT_HINT_MAX_LEN);
	if (!local)
		return ENOMEM;

	rc = decode_one_hint(local, rf, &obj_hint, &type);
	if (rc)
		D_GOTO(out, rc);

	rc = daos_obj_get_oclass(dfs->coh, type, obj_hint, 0, cid);
	if (rc) {
		D_ERROR("daos_obj_get_oclass() failed "DF_RC"\n", DP_RC(rc));
		return daos_der2errno(rc);
	}
out:
	D_FREE(local);
	return rc;
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
oid_gen(dfs_t *dfs, daos_oclass_id_t oclass, bool file, daos_obj_id_t *oid)
{
	enum daos_otype_t type = DAOS_OT_MULTI_HASHED;
	int rc;

	D_MUTEX_LOCK(&dfs->lock);
	/** If we ran out of local OIDs, alloc one from the container */
	if (dfs->oid.hi >= MAX_OID_HI) {
		/** Allocate an OID for the namespace */
		rc = daos_cont_alloc_oids(dfs->coh, 1, &dfs->oid.lo, NULL);
		if (rc) {
			D_ERROR("daos_cont_alloc_oids() Failed (%d)\n", rc);
			D_MUTEX_UNLOCK(&dfs->lock);
			return daos_der2errno(rc);
		}
		dfs->oid.hi = 0;
	}

	/** set oid and lo, bump the current hi value */
	oid->lo = dfs->oid.lo;
	oid->hi = dfs->oid.hi++;
	D_MUTEX_UNLOCK(&dfs->lock);

	/** if a regular file, use UINT64 typed dkeys for the array object */
	if (file)
		type = DAOS_OT_ARRAY_BYTE;

	/** generate the daos object ID (set the DAOS owned bits) */
	rc = daos_obj_generate_oid(dfs->coh, oid, type, oclass,
				   file ? dfs->file_oclass_hint : dfs->dir_oclass_hint, 0);
	if (rc) {
		if (file)
			D_ERROR("file hint = %u, oclass = %u\n", dfs->file_oclass_hint, oclass);
		else
			D_ERROR("dir hint = %u, oclass = %u\n", dfs->dir_oclass_hint, oclass);
		D_ERROR("daos_obj_generate_oid() failed "DF_RC"\n", DP_RC(rc));
		return daos_der2errno(rc);
	}

	return 0;
}

static char *
concat(const char *s1, const char *s2)
{
	char *result = NULL;

	D_ASPRINTF(result, "%s%s", s1, s2);
	if (result == NULL)
		return NULL;

	return result;
}

static int
fetch_entry(dfs_layout_ver_t ver, daos_handle_t oh, daos_handle_t th, const char *name, size_t len,
	    bool fetch_sym, bool *exists, struct dfs_entry *entry, int xnr, char *xnames[],
	    void *xvals[], daos_size_t *xsizes)
{
	d_sg_list_t	l_sgl, *sgl;
	d_iov_t		sg_iovs[INODE_AKEYS];
	daos_iod_t	l_iod, *iod;
	daos_recx_t	recx;
	daos_key_t	dkey;
	unsigned int	i;
	char		**pxnames = NULL;
	d_iov_t		*sg_iovx = NULL;
	d_sg_list_t	*sgls = NULL;
	daos_iod_t	*iods = NULL;
	int		rc;

	D_ASSERT(name);

	/** TODO - not supported yet */
	if (strcmp(name, ".") == 0)
		D_ASSERT(0);

	if (xnr) {
		D_ALLOC_ARRAY(pxnames, xnr);
		if (pxnames == NULL)
			D_GOTO(out, rc = ENOMEM);

		D_ALLOC_ARRAY(sg_iovx, xnr);
		if (sg_iovx == NULL)
			D_GOTO(out, rc = ENOMEM);

		D_ALLOC_ARRAY(sgls, xnr + 1);
		if (sgls == NULL)
			D_GOTO(out, rc = ENOMEM);

		D_ALLOC_ARRAY(iods, xnr + 1);
		if (iods == NULL)
			D_GOTO(out, rc = ENOMEM);

		for (i = 0; i < xnr; i++) {
			pxnames[i] = concat("x:", xnames[i]);
			if (pxnames[i] == NULL)
				D_GOTO(out, rc = ENOMEM);

			d_iov_set(&iods[i].iod_name, pxnames[i],
				  strlen(pxnames[i]));
			iods[i].iod_nr		= 1;
			iods[i].iod_recxs	= NULL;
			iods[i].iod_type	= DAOS_IOD_SINGLE;
			iods[i].iod_size	= xsizes[i];

			d_iov_set(&sg_iovx[i], xvals[i], xsizes[i]);
			sgls[i].sg_nr		= 1;
			sgls[i].sg_nr_out	= 0;
			sgls[i].sg_iovs		= &sg_iovx[i];
		}

		sgl = &sgls[xnr];
		iod = &iods[xnr];
	} else {
		sgl = &l_sgl;
		iod = &l_iod;
	}

	d_iov_set(&dkey, (void *)name, len);
	d_iov_set(&iod->iod_name, INODE_AKEY_NAME, sizeof(INODE_AKEY_NAME) - 1);
	iod->iod_nr	= 1;
	recx.rx_idx	= 0;
	recx.rx_nr	= END_IDX;
	iod->iod_recxs	= &recx;
	iod->iod_type	= DAOS_IOD_ARRAY;
	iod->iod_size	= 1;

	i = 0;
	d_iov_set(&sg_iovs[i++], &entry->mode, sizeof(mode_t));
	d_iov_set(&sg_iovs[i++], &entry->oid, sizeof(daos_obj_id_t));
	d_iov_set(&sg_iovs[i++], &entry->mtime, sizeof(uint64_t));
	d_iov_set(&sg_iovs[i++], &entry->ctime, sizeof(uint64_t));
	d_iov_set(&sg_iovs[i++], &entry->chunk_size, sizeof(daos_size_t));
	d_iov_set(&sg_iovs[i++], &entry->oclass, sizeof(daos_oclass_id_t));
	d_iov_set(&sg_iovs[i++], &entry->mtime_nano, sizeof(uint64_t));
	d_iov_set(&sg_iovs[i++], &entry->ctime_nano, sizeof(uint64_t));
	d_iov_set(&sg_iovs[i++], &entry->uid, sizeof(uid_t));
	d_iov_set(&sg_iovs[i++], &entry->gid, sizeof(gid_t));
	d_iov_set(&sg_iovs[i++], &entry->value_len, sizeof(daos_size_t));
	d_iov_set(&sg_iovs[i++], &entry->obj_hlc, sizeof(uint64_t));

	sgl->sg_nr	= i;
	sgl->sg_nr_out	= 0;
	sgl->sg_iovs	= sg_iovs;

	rc = daos_obj_fetch(oh, th, DAOS_COND_DKEY_FETCH, &dkey, xnr + 1, iods ? iods : iod,
			    sgls ? sgls : sgl, NULL, NULL);
	if (rc == -DER_NONEXIST) {
		*exists = false;
		D_GOTO(out, rc = 0);
	} else if (rc) {
		D_ERROR("Failed to fetch entry %s "DF_RC"\n", name, DP_RC(rc));
		D_GOTO(out, rc = daos_der2errno(rc));
	}

	for (i = 0; i < xnr; i++)
		xsizes[i] = iods[i].iod_size;

	if (fetch_sym && S_ISLNK(entry->mode)) {
		char		*value;
		daos_size_t	val_len;

		/** symlink is empty */
		if (entry->value_len == 0)
			D_GOTO(out, rc = EIO);
		val_len = entry->value_len;
		D_ALLOC(value, val_len + 1);
		if (value == NULL)
			D_GOTO(out, rc = ENOMEM);

		d_iov_set(&iod->iod_name, SLINK_AKEY_NAME, sizeof(SLINK_AKEY_NAME) - 1);
		iod->iod_nr	= 1;
		iod->iod_recxs	= NULL;
		iod->iod_type	= DAOS_IOD_SINGLE;
		iod->iod_size	= DAOS_REC_ANY;

		d_iov_set(&sg_iovs[0], value, val_len);
		sgl->sg_nr	= 1;
		sgl->sg_nr_out	= 0;
		sgl->sg_iovs	= sg_iovs;

		rc = daos_obj_fetch(oh, th, DAOS_COND_DKEY_FETCH, &dkey, 1, iod, sgl, NULL, NULL);
		if (rc) {
			D_FREE(value);
			if (rc == -DER_NONEXIST) {
				*exists = false;
				D_GOTO(out, rc = 0);
			}
			D_ERROR("Failed to fetch entry %s "DF_RC"\n", name, DP_RC(rc));
			D_GOTO(out, rc = daos_der2errno(rc));
		}

		/** make sure that the akey value size matches what is in the inode */
		if (iod->iod_size != val_len) {
			D_ERROR("Symlink value length inconsistent with inode data\n");
			D_GOTO(out, rc = EIO);
		}
		value[val_len] = 0;

		if (entry->value_len != 0) {
			entry->value = value;
		} else {
			D_ERROR("Failed to load value for symlink\n");
			D_FREE(value);
			D_GOTO(out, rc = EIO);
		}
	}

	if (sgl->sg_nr_out == 0)
		*exists = false;
	else
		*exists = true;
out:
	if (xnr) {
		if (pxnames) {
			for (i = 0; i < xnr; i++)
				D_FREE(pxnames[i]);
			D_FREE(pxnames);
		}
		D_FREE(sg_iovx);
		D_FREE(sgls);
		D_FREE(iods);
	}
	return rc;
}

static int
remove_entry(dfs_t *dfs, daos_handle_t th, daos_handle_t parent_oh,
	     const char *name, size_t len, struct dfs_entry entry)
{
	daos_key_t	dkey;
	daos_handle_t	oh;
	int		rc;

	if (S_ISLNK(entry.mode))
		goto punch_entry;

	rc = daos_obj_open(dfs->coh, entry.oid, DAOS_OO_RW, &oh, NULL);
	if (rc)
		return daos_der2errno(rc);

	rc = daos_obj_punch(oh, th, 0, NULL);
	if (rc) {
		daos_obj_close(oh, NULL);
		return daos_der2errno(rc);
	}

	rc = daos_obj_close(oh, NULL);
	if (rc)
		return daos_der2errno(rc);

punch_entry:
	d_iov_set(&dkey, (void *)name, len);
	/** we only need a conditional dkey punch if we are not using a DTX */
	rc = daos_obj_punch_dkeys(parent_oh, th, dfs->use_dtx ? 0 : DAOS_COND_PUNCH, 1, &dkey,
				  NULL);
	return daos_der2errno(rc);
}

static int
insert_entry(dfs_layout_ver_t ver, daos_handle_t oh, daos_handle_t th, const char *name, size_t len,
	     uint64_t flags, struct dfs_entry *entry)
{
	d_sg_list_t	sgls[2];
	d_iov_t		sg_iovs[INODE_AKEYS];
	d_iov_t		sym_iov;
	daos_iod_t	iods[2];
	daos_recx_t	recx;
	daos_key_t	dkey;
	unsigned int	i;
	unsigned int	nr_iods;
	int		rc;

	d_iov_set(&dkey, (void *)name, len);
	d_iov_set(&iods[0].iod_name, INODE_AKEY_NAME, sizeof(INODE_AKEY_NAME) - 1);
	iods[0].iod_nr		= 1;
	recx.rx_idx		= 0;
	recx.rx_nr		= END_IDX;
	iods[0].iod_recxs	= &recx;
	iods[0].iod_type	= DAOS_IOD_ARRAY;
	iods[0].iod_size	= 1;

	i = 0;
	d_iov_set(&sg_iovs[i++], &entry->mode, sizeof(mode_t));
	d_iov_set(&sg_iovs[i++], &entry->oid, sizeof(daos_obj_id_t));
	d_iov_set(&sg_iovs[i++], &entry->mtime, sizeof(uint64_t));
	d_iov_set(&sg_iovs[i++], &entry->ctime, sizeof(uint64_t));
	d_iov_set(&sg_iovs[i++], &entry->chunk_size, sizeof(daos_size_t));
	d_iov_set(&sg_iovs[i++], &entry->oclass, sizeof(daos_oclass_id_t));
	d_iov_set(&sg_iovs[i++], &entry->mtime_nano, sizeof(uint64_t));
	d_iov_set(&sg_iovs[i++], &entry->ctime_nano, sizeof(uint64_t));
	d_iov_set(&sg_iovs[i++], &entry->uid, sizeof(uid_t));
	d_iov_set(&sg_iovs[i++], &entry->gid, sizeof(gid_t));
	/** Add file size / symlink length. for now, file size cached in the entry is 0. */
	d_iov_set(&sg_iovs[i++], &entry->value_len, sizeof(daos_size_t));
	d_iov_set(&sg_iovs[i++], &entry->obj_hlc, sizeof(uint64_t));

	/** add the symlink as a separate akey */
	if (S_ISLNK(entry->mode)) {
		nr_iods = 2;
		d_iov_set(&iods[1].iod_name, SLINK_AKEY_NAME, sizeof(SLINK_AKEY_NAME) - 1);
		iods[1].iod_nr		= 1;
		iods[1].iod_recxs	= NULL;
		iods[1].iod_type	= DAOS_IOD_SINGLE;
		iods[1].iod_size	= entry->value_len;

		d_iov_set(&sym_iov, entry->value, entry->value_len);
		sgls[1].sg_nr = 1;
		sgls[1].sg_nr_out = 0;
		sgls[1].sg_iovs = &sym_iov;
	} else {
		nr_iods = 1;
	}

	sgls[0].sg_nr		= i;
	sgls[0].sg_nr_out	= 0;
	sgls[0].sg_iovs		= sg_iovs;

	rc = daos_obj_update(oh, th, flags, &dkey, nr_iods, iods, sgls, NULL);
	if (rc) {
		/** don't log error if conditional failed */
		if (rc != -DER_EXIST && rc != -DER_NO_PERM)
			D_ERROR("Failed to insert entry '%s', "DF_RC"\n", name, DP_RC(rc));
		return daos_der2errno(rc);
	}
	return 0;
}

static int
get_num_entries(daos_handle_t oh, daos_handle_t th, uint32_t *nr,
		bool check_empty)
{
	daos_key_desc_t	kds[ENUM_DESC_NR];
	daos_anchor_t	anchor = {0};
	uint32_t	key_nr = 0;
	d_sg_list_t	sgl;
	d_iov_t		iov;
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
			return daos_der2errno(rc);

		if (number == 0)
			continue;

		key_nr += number;

		/** if we just want to check if entries exist, break now */
		if (check_empty)
			break;
	}

	*nr = key_nr;
	return 0;
}

static int
update_stbuf_times(struct dfs_entry entry, daos_epoch_t max_epoch, struct stat *stbuf,
		   uint64_t *obj_hlc)
{
	struct timespec obj_mtime, entry_mtime;
	int		rc;

	/** the file/dir have not been touched, so the entry times are accurate */
	if (max_epoch == 0) {
		stbuf->st_ctim.tv_sec = entry.ctime;
		stbuf->st_ctim.tv_nsec = entry.ctime_nano;
		stbuf->st_mtim.tv_sec = entry.mtime;
		stbuf->st_mtim.tv_nsec = entry.mtime_nano;
		return 0;
	}

	rc = d_hlc2timespec(max_epoch, &obj_mtime);
	if (rc) {
		D_ERROR("d_hlc2timespec() failed "DF_RC"\n", DP_RC(rc));
		return daos_der2errno(rc);
	}

	if (obj_hlc)
		*obj_hlc = max_epoch;

	rc = d_hlc2timespec(entry.obj_hlc, &entry_mtime);
	if (rc) {
		D_ERROR("d_hlc2timespec() failed "DF_RC"\n", DP_RC(rc));
		return daos_der2errno(rc);
	}

	/** ctime should be the greater of the entry and object hlc */
	stbuf->st_ctim.tv_sec = entry.ctime;
	stbuf->st_ctim.tv_nsec = entry.ctime_nano;
	if (tspec_gt(obj_mtime, stbuf->st_ctim)) {
		stbuf->st_ctim.tv_sec = obj_mtime.tv_sec;
		stbuf->st_ctim.tv_nsec = obj_mtime.tv_nsec;
	}

	/*
	 * mtime is not like ctime since user can update it manually. So returning the larger mtime
	 * like ctime would not work since the user can manually set the mtime to the past.
	 */
	if (obj_mtime.tv_sec == entry_mtime.tv_sec && obj_mtime.tv_nsec == entry_mtime.tv_nsec) {
		/*
		 * internal mtime entry set through a user set mtime and is up to date with the
		 * object epoch time, which means that the user set mtime in the inode entry is the
		 * correct value to return.
		 */
		stbuf->st_mtim.tv_sec = entry.mtime;
		stbuf->st_mtim.tv_nsec = entry.mtime_nano;
	} else {
		/*
		 * the user has not updated the mtime explicitly or the object itself was modified
		 * after an explicit mtime update.
		 */
		stbuf->st_mtim.tv_sec = obj_mtime.tv_sec;
		stbuf->st_mtim.tv_nsec = obj_mtime.tv_nsec;
	}

	return 0;
}

static int
entry_stat(dfs_t *dfs, daos_handle_t th, daos_handle_t oh, const char *name, size_t len,
	   struct dfs_obj *obj, bool get_size, struct stat *stbuf, uint64_t *obj_hlc)
{
	struct dfs_entry	entry = {0};
	bool			exists;
	daos_size_t		size;
	int			rc;

	memset(stbuf, 0, sizeof(struct stat));

	/** Check if parent has the entry. */
	rc = fetch_entry(dfs->layout_v, oh, th, name, len, false, &exists, &entry, 0,
			 NULL, NULL, NULL);
	if (rc)
		return rc;

	if (!exists)
		return ENOENT;

	if (obj && (obj->oid.hi != entry.oid.hi || obj->oid.lo != entry.oid.lo))
		return ENOENT;

	switch (entry.mode & S_IFMT) {
	case S_IFDIR:
	{
		daos_handle_t	dir_oh;
		daos_epoch_t	ep;

		size = sizeof(entry);

		/** check if dir is empty */
		rc = daos_obj_open(dfs->coh, entry.oid, DAOS_OO_RO, &dir_oh, NULL);
		if (rc) {
			D_ERROR("daos_obj_open() Failed, "DF_RC"\n", DP_RC(rc));
			return daos_der2errno(rc);
		}

		rc = daos_obj_query_max_epoch(dir_oh, th, &ep, NULL);
		if (rc) {
			daos_obj_close(dir_oh, NULL);
			return daos_der2errno(rc);
		}

		rc = daos_obj_close(dir_oh, NULL);
		if (rc)
			return daos_der2errno(rc);

		/** object was updated since creation */
		rc = update_stbuf_times(entry, ep, stbuf, obj_hlc);
		if (rc)
			return rc;
		break;
	}
	case S_IFREG:
	{
		daos_array_stbuf_t	array_stbuf = {0};

		stbuf->st_blksize = entry.chunk_size ? entry.chunk_size : dfs->attr.da_chunk_size;

		/** don't stat the array and use the entry mtime */
		if (!get_size) {
			stbuf->st_mtim.tv_sec = entry.mtime;
			stbuf->st_mtim.tv_nsec = entry.mtime_nano;
			size = 0;
			break;
		}

		if (obj) {
			rc = daos_array_stat(obj->oh, th, &array_stbuf, NULL);
			if (rc)
				return daos_der2errno(rc);
		} else {
			daos_handle_t	file_oh;

			rc = daos_array_open_with_attr(dfs->coh, entry.oid, th, DAOS_OO_RO, 1,
						       entry.chunk_size ? entry.chunk_size :
						       dfs->attr.da_chunk_size, &file_oh, NULL);
			if (rc) {
				D_ERROR("daos_array_open_with_attr() failed "DF_RC"\n", DP_RC(rc));
				return daos_der2errno(rc);
			}

			rc = daos_array_stat(file_oh, th, &array_stbuf, NULL);
			if (rc) {
				daos_array_close(file_oh, NULL);
				return daos_der2errno(rc);
			}

			rc = daos_array_close(file_oh, NULL);
			if (rc)
				return daos_der2errno(rc);
		}

		size = array_stbuf.st_size;
		rc = update_stbuf_times(entry, array_stbuf.st_max_epoch, stbuf, obj_hlc);
		if (rc)
			return rc;

		/*
		 * TODO - this is not accurate since it does not account for sparse files or file
		 * metadata or xattributes.
		 */
		stbuf->st_blocks = (size + (1 << 9) - 1) >> 9;
		break;
	}
	case S_IFLNK:
		size = entry.value_len;
		D_FREE(entry.value);
		stbuf->st_mtim.tv_sec = entry.mtime;
		stbuf->st_mtim.tv_nsec = entry.mtime_nano;
		stbuf->st_ctim.tv_sec = entry.ctime;
		stbuf->st_ctim.tv_nsec = entry.ctime_nano;
		break;
	default:
		D_ERROR("Invalid entry type (not a dir, file, symlink).\n");
		return EINVAL;
	}

	stbuf->st_nlink = 1;
	stbuf->st_size = size;
	stbuf->st_mode = entry.mode;
	stbuf->st_uid = entry.uid;
	stbuf->st_gid = entry.gid;
	if (tspec_gt(stbuf->st_ctim, stbuf->st_mtim)) {
		stbuf->st_atim.tv_sec = stbuf->st_ctim.tv_sec;
		stbuf->st_atim.tv_nsec = stbuf->st_ctim.tv_nsec;
	} else {
		stbuf->st_atim.tv_sec = stbuf->st_mtim.tv_sec;
		stbuf->st_atim.tv_nsec = stbuf->st_mtim.tv_nsec;
	}
	return 0;
}

static inline int
check_name(const char *name, size_t *_len)
{
	size_t len;

	*_len = 0;

	if (name == NULL || strchr(name, '/'))
		return EINVAL;

	len = strnlen(name, DFS_MAX_NAME + 1);
	if (len > DFS_MAX_NAME)
		return EINVAL;

	*_len = len;
	return 0;
}

static int
check_access(uid_t c_uid, gid_t c_gid, uid_t uid, gid_t gid, mode_t mode, int mask)
{
	mode_t	base_mask;

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

static int
open_file(dfs_t *dfs, dfs_obj_t *parent, int flags, daos_oclass_id_t cid,
	  daos_size_t chunk_size, struct dfs_entry *entry, daos_size_t *size,
	  size_t len, dfs_obj_t *file)
{
	bool			exists;
	int			daos_mode;
	bool			oexcl = flags & O_EXCL;
	bool			ocreat = flags & O_CREAT;
	int			rc;

	if (ocreat) {
		struct timespec		now;

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
			D_ERROR("daos_array_open_with_attr() failed "DF_RC"\n", DP_RC(rc));
			return daos_der2errno(rc);
		}

		/** Create and insert entry in parent dir object. */
		entry->mode = file->mode;
		rc = clock_gettime(CLOCK_REALTIME, &now);
		if (rc)
			return errno;
		entry->mtime = entry->ctime = now.tv_sec;
		entry->mtime_nano = entry->ctime_nano = now.tv_nsec;
		entry->chunk_size = chunk_size;

		rc = insert_entry(dfs->layout_v, parent->oh, DAOS_TX_NONE, file->name, len,
				  DAOS_COND_DKEY_INSERT, entry);
		if (rc == EEXIST && !oexcl) {
			int rc2;

			/** just try fetching entry to open the file */
			rc2 = daos_array_close(file->oh, NULL);
			if (rc2) {
				D_ERROR("daos_array_close() failed "DF_RC"\n", DP_RC(rc2));
				return daos_der2errno(rc2);
			}
		} else if (rc) {
			int rc2;

			rc2 = daos_array_close(file->oh, NULL);
			if (rc2)
				D_ERROR("daos_array_close() failed "DF_RC"\n", DP_RC(rc2));
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
	rc = daos_array_open_with_attr(dfs->coh, entry->oid, DAOS_TX_NONE, daos_mode, 1,
				       entry->chunk_size, &file->oh, NULL);
	if (rc != 0) {
		D_ERROR("daos_array_open_with_attr() failed, "DF_RC"\n", DP_RC(rc));
		return daos_der2errno(rc);
	}

	if (flags & O_TRUNC) {
		rc = daos_array_set_size(file->oh, DAOS_TX_NONE, 0, NULL);
		if (rc) {
			D_ERROR("Failed to truncate file "DF_RC"\n", DP_RC(rc));
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

/*
 * create a dir object. If caller passes parent obj, we check for existence of
 * object first.
 */
static inline int
create_dir(dfs_t *dfs, dfs_obj_t *parent, daos_oclass_id_t cid, dfs_obj_t *dir)
{
	int			rc;

	/** set oclass for dir. order: API, parent dir, cont default */
	if (cid == 0) {
		if (parent->d.oclass == 0)
			cid = dfs->attr.da_dir_oclass_id;
		else
			cid = parent->d.oclass;
	}

	/** Allocate an OID for the dir - local operation */
	rc = oid_gen(dfs, cid, false, &dir->oid);
	if (rc != 0)
		return rc;

	/** Open the Object - local operation */
	rc = daos_obj_open(dfs->coh, dir->oid, DAOS_OO_RW, &dir->oh, NULL);
	if (rc) {
		D_ERROR("daos_obj_open() Failed, "DF_RC"\n", DP_RC(rc));
		return daos_der2errno(rc);
	}

	return 0;
}

static int
open_dir(dfs_t *dfs, dfs_obj_t *parent, int flags, daos_oclass_id_t cid,
	 struct dfs_entry *entry, size_t len, dfs_obj_t *dir)
{
	bool			exists;
	int			daos_mode;
	bool			oexcl = flags & O_EXCL;
	bool			ocreat = flags & O_CREAT;
	daos_handle_t		parent_oh;
	int			rc;

	parent_oh = parent ? parent->oh : dfs->super_oh;

	if (ocreat) {
		struct timespec	now;

		D_ASSERT(parent);
		/** this generates the OID and opens the object */
		rc = create_dir(dfs, parent, cid, dir);
		if (rc)
			return rc;

		entry->oid = dir->oid;
		entry->mode = dir->mode;
		rc = clock_gettime(CLOCK_REALTIME, &now);
		if (rc) {
			rc = errno;
			daos_obj_close(dir->oh, NULL);
			return rc;
		}
		entry->mtime = entry->ctime = now.tv_sec;
		entry->mtime_nano = entry->ctime_nano = now.tv_nsec;
		entry->chunk_size = parent->d.chunk_size;
		entry->oclass = parent->d.oclass;

		/** since it's a single conditional op, we don't need a DTX */
		rc = insert_entry(dfs->layout_v, parent->oh, DAOS_TX_NONE, dir->name, len,
				  DAOS_COND_DKEY_INSERT, entry);
		if (rc == EEXIST && !oexcl) {
			/** just try fetching entry to open the file */
			daos_obj_close(dir->oh, NULL);
		} else if (rc) {
			daos_obj_close(dir->oh, NULL);
			D_DEBUG(DB_TRACE, "Insert dir entry %s failed (%d)\n", dir->name, rc);
			return rc;
		} else {
			/** Success, commit */
			D_ASSERT(rc == 0);
			dir->d.chunk_size = entry->chunk_size;
			dir->d.oclass = entry->oclass;
			return 0;
		}
	}

	/* Check if parent has the dirname entry */
	rc = fetch_entry(dfs->layout_v, parent_oh, DAOS_TX_NONE, dir->name, len, false, &exists,
			 entry, 0, NULL, NULL, NULL);
	if (rc) {
		D_DEBUG(DB_TRACE, "fetch_entry %s failed %d.\n", dir->name, rc);
		return rc;
	}

	if (!exists)
		return ENOENT;

	/* Check that the opened object is the type that's expected. */
	if (!S_ISDIR(entry->mode))
		return ENOTDIR;

	daos_mode = get_daos_obj_mode(flags);
	if (daos_mode == -1)
		return EINVAL;

	rc = daos_obj_open(dfs->coh, entry->oid, daos_mode, &dir->oh, NULL);
	if (rc) {
		D_ERROR("daos_obj_open() Failed, "DF_RC"\n", DP_RC(rc));
		return daos_der2errno(rc);
	}
	dir->mode = entry->mode;
	oid_cp(&dir->oid, entry->oid);
	dir->d.chunk_size = entry->chunk_size;
	dir->d.oclass = entry->oclass;
	return 0;
}

static int
open_symlink(dfs_t *dfs, dfs_obj_t *parent, int flags, daos_oclass_id_t cid,
	     const char *value, struct dfs_entry *entry, size_t len,
	     dfs_obj_t *sym)
{
	size_t			value_len;
	int			rc;

	if (flags & O_CREAT) {
		struct timespec	now;

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
		rc = clock_gettime(CLOCK_REALTIME, &now);
		if (rc)
			return errno;
		entry->mtime = entry->ctime = now.tv_sec;
		entry->mtime_nano = entry->ctime_nano = now.tv_nsec;
		D_STRNDUP(sym->value, value, value_len + 1);
		if (sym->value == NULL)
			return ENOMEM;
		entry->value = sym->value;
		entry->value_len = value_len;

		rc = insert_entry(dfs->layout_v, parent->oh, DAOS_TX_NONE, sym->name, len,
				  DAOS_COND_DKEY_INSERT, entry);
		if (rc == EEXIST) {
			D_FREE(sym->value);
		} else if (rc != 0) {
			D_FREE(sym->value);
			D_ERROR("Inserting entry '%s' failed: %d (%s)\n",
				sym->name, rc, strerror(rc));
		}
		return rc;
	}

	return ENOTSUP;
}

static void
set_daos_iod(bool create, daos_iod_t *iod, char *buf, size_t size)
{
	d_iov_set(&iod->iod_name, buf, strlen(buf));
	iod->iod_nr	= 1;
	iod->iod_size	= DAOS_REC_ANY;
	iod->iod_recxs	= NULL;
	iod->iod_type	= DAOS_IOD_SINGLE;

	if (create)
		iod->iod_size = size;
}

static void
set_sb_params(bool for_update, daos_iod_t *iods, daos_key_t *dkey)
{
	d_iov_set(dkey, SB_DKEY, sizeof(SB_DKEY) - 1);
	set_daos_iod(for_update, &iods[MAGIC_IDX], MAGIC_NAME, sizeof(dfs_magic_t));
	set_daos_iod(for_update, &iods[SB_VER_IDX], SB_VER_NAME, sizeof(dfs_sb_ver_t));
	set_daos_iod(for_update, &iods[LAYOUT_VER_IDX], LAYOUT_VER_NAME, sizeof(dfs_layout_ver_t));
	set_daos_iod(for_update, &iods[CS_IDX], CS_NAME, sizeof(daos_size_t));
	set_daos_iod(for_update, &iods[OC_IDX], OC_NAME, sizeof(daos_oclass_id_t));
	set_daos_iod(for_update, &iods[FILE_OC_IDX], FILE_OC_NAME, sizeof(daos_oclass_id_t));
	set_daos_iod(for_update, &iods[DIR_OC_IDX], DIR_OC_NAME, sizeof(daos_oclass_id_t));
	set_daos_iod(for_update, &iods[CONT_MODE_IDX], CONT_MODE_NAME, sizeof(uint32_t));
	set_daos_iod(for_update, &iods[CONT_HINT_IDX], CONT_HINT_NAME, DAOS_CONT_HINT_MAX_LEN);
}

static int
open_sb(daos_handle_t coh, bool create, bool punch, int omode, daos_obj_id_t super_oid,
	dfs_attr_t *attr, daos_handle_t *oh, dfs_layout_ver_t *ver)
{
	d_sg_list_t		sgls[SB_AKEYS];
	d_iov_t			sg_iovs[SB_AKEYS];
	daos_iod_t		iods[SB_AKEYS];
	daos_key_t		dkey;
	dfs_magic_t		magic;
	dfs_sb_ver_t		sb_ver;
	dfs_layout_ver_t	layout_ver;
	daos_size_t		chunk_size = 0;
	daos_oclass_id_t	oclass = OC_UNKNOWN;
	daos_oclass_id_t	dir_oclass = OC_UNKNOWN;
	daos_oclass_id_t	file_oclass = OC_UNKNOWN;
	uint32_t		mode;
	char			hints[DAOS_CONT_HINT_MAX_LEN];
	int			i, rc;

	D_ASSERT(attr);

	/** Open SB object */
	rc = daos_obj_open(coh, super_oid, omode, oh, NULL);
	if (rc) {
		D_ERROR("daos_obj_open() Failed, "DF_RC"\n", DP_RC(rc));
		return daos_der2errno(rc);
	}

	d_iov_set(&sg_iovs[MAGIC_IDX], &magic, sizeof(dfs_magic_t));
	d_iov_set(&sg_iovs[SB_VER_IDX], &sb_ver, sizeof(dfs_sb_ver_t));
	d_iov_set(&sg_iovs[LAYOUT_VER_IDX], &layout_ver, sizeof(dfs_layout_ver_t));
	d_iov_set(&sg_iovs[CS_IDX], &chunk_size, sizeof(daos_size_t));
	d_iov_set(&sg_iovs[OC_IDX], &oclass, sizeof(daos_oclass_id_t));
	d_iov_set(&sg_iovs[FILE_OC_IDX], &file_oclass, sizeof(daos_oclass_id_t));
	d_iov_set(&sg_iovs[DIR_OC_IDX], &dir_oclass, sizeof(daos_oclass_id_t));
	d_iov_set(&sg_iovs[CONT_MODE_IDX], &mode, sizeof(uint32_t));

	for (i = 0; i < SB_AKEYS; i++) {
		sgls[i].sg_nr		= 1;
		sgls[i].sg_nr_out	= 0;
		sgls[i].sg_iovs		= &sg_iovs[i];
	}

	set_sb_params(create, iods, &dkey);

	if (punch) {
		rc = daos_obj_punch_dkeys(*oh, DAOS_TX_NONE, 0, 1, &dkey, NULL);
		if (rc) {
			D_ERROR("SB punch failed: "DF_RC"\n", DP_RC(rc));
			D_GOTO(err, rc = daos_der2errno(rc));
		}
	}

	/** create the SB and exit */
	if (create) {
		int num_iods = SB_AKEYS;
		size_t hint_len = strlen(attr->da_hints);

		/** adjust the IOD for the hints string to the actual size */
		if (hint_len) {
			set_daos_iod(true, &iods[CONT_HINT_IDX], CONT_HINT_NAME, hint_len + 1);
			d_iov_set(&sg_iovs[CONT_HINT_IDX], attr->da_hints, hint_len + 1);
		} else {
			num_iods--;
		}

		magic = DFS_SB_MAGIC;
		sb_ver = DFS_SB_VERSION;
		layout_ver = DFS_LAYOUT_VERSION;

		if (attr->da_chunk_size != 0)
			chunk_size = attr->da_chunk_size;
		else
			chunk_size = DFS_DEFAULT_CHUNK_SIZE;

		oclass = attr->da_oclass_id;
		dir_oclass = attr->da_dir_oclass_id;
		file_oclass = attr->da_file_oclass_id;
		mode = attr->da_mode;

		rc = daos_obj_update(*oh, DAOS_TX_NONE, DAOS_COND_DKEY_INSERT, &dkey, num_iods,
				     iods, sgls, NULL);
		if (rc) {
			D_ERROR("Failed to create DFS superblock "DF_RC"\n", DP_RC(rc));
			D_GOTO(err, rc = daos_der2errno(rc));
		}

		return 0;
	}

	/** set hints value to max */
	d_iov_set(&sg_iovs[CONT_HINT_IDX], &hints, DAOS_CONT_HINT_MAX_LEN);
	set_daos_iod(false, &iods[CONT_HINT_IDX], CONT_HINT_NAME, DAOS_CONT_HINT_MAX_LEN);

	/* Fetch the values and verify SB */
	rc = daos_obj_fetch(*oh, DAOS_TX_NONE, 0, &dkey, SB_AKEYS, iods, sgls, NULL, NULL);
	if (rc) {
		D_ERROR("Failed to fetch SB info, "DF_RC"\n", DP_RC(rc));
		D_GOTO(err, rc = daos_der2errno(rc));
	}

	/** check if SB info exists */
	if (iods[MAGIC_IDX].iod_size == 0) {
		rc = ENOENT;
		D_ERROR("SB does not exist: %d (%s)\n", rc, strerror(rc));
		D_GOTO(err, rc);
	}

	if (magic != DFS_SB_MAGIC) {
		rc = EINVAL;
		D_ERROR("SB MAGIC verification failed: %d (%s)\n", rc, strerror(rc));
		D_GOTO(err, rc);
	}

	/** check version compatibility */
	if (iods[SB_VER_IDX].iod_size != sizeof(sb_ver) || sb_ver > DFS_SB_VERSION) {
		rc = EINVAL;
		D_ERROR("Incompatible SB version: %d (%s)\n", rc, strerror(rc));
		D_GOTO(err, rc);
	}

	if (iods[LAYOUT_VER_IDX].iod_size != sizeof(layout_ver) ||
	    layout_ver != DFS_LAYOUT_VERSION) {
		rc = EINVAL;
		D_ERROR("Incompatible DFS Layout version %d: %d (%s)\n", layout_ver, rc,
			strerror(rc));
		D_GOTO(err, rc);
	}

	D_DEBUG(DB_ALL, "DFS Container Layout version: %d\n", layout_ver);
	D_DEBUG(DB_ALL, "DFS Library Layout version: %d\n", DFS_LAYOUT_VERSION);

	*ver = layout_ver;
	attr->da_chunk_size = (chunk_size) ? chunk_size : DFS_DEFAULT_CHUNK_SIZE;
	attr->da_oclass_id = oclass;
	attr->da_dir_oclass_id = dir_oclass;
	attr->da_file_oclass_id = file_oclass;
	attr->da_mode = mode;
	if (iods[CONT_HINT_IDX].iod_size != 0)
		strcpy(attr->da_hints, hints);

	return 0;
err:
	daos_obj_close(*oh, NULL);
	return rc;
}

int
dfs_get_sb_layout(daos_key_t *dkey, daos_iod_t *iods[], int *akey_count,
		  int *dfs_entry_key_size, int *dfs_entry_size)
{
	struct dfs_entry entry;

	if (dkey == NULL || akey_count == NULL)
		return EINVAL;

	D_ALLOC_ARRAY(*iods, SB_AKEYS);
	if (*iods == NULL)
		return ENOMEM;

	*akey_count = SB_AKEYS;
	*dfs_entry_key_size = sizeof(INODE_AKEY_NAME) - 1;
	/** Can't just use sizeof(struct dfs_entry because it's not accurate */
	*dfs_entry_size =
	    D_ALIGNUP(sizeof(entry.mode) + sizeof(entry.oid) + sizeof(entry.mtime) +
			  sizeof(entry.ctime) + sizeof(entry.chunk_size) + sizeof(entry.oclass) +
			  sizeof(entry.mtime_nano) + sizeof(entry.ctime_nano) + sizeof(entry.uid) +
			  sizeof(entry.gid) + sizeof(entry.value_len) + sizeof(entry.obj_hlc),
		      32);

	set_sb_params(true, *iods, dkey);

	return 0;
}

int
dfs_cont_create(daos_handle_t poh, uuid_t *cuuid, dfs_attr_t *attr,
		daos_handle_t *_coh, dfs_t **_dfs)
{
	daos_handle_t		coh, super_oh;
	struct dfs_entry	entry = {0};
	daos_prop_t		*prop = NULL;
	daos_oclass_hints_t	dir_oclass_hint = 0;
	uint64_t		rf;
	daos_cont_info_t	co_info;
	dfs_t			*dfs;
	dfs_attr_t		dattr = {0};
	char			str[37];
	struct daos_prop_co_roots roots;
	int			rc, rc2;
	struct daos_prop_entry  *dpe;
	struct timespec		now;
	uint32_t		cid_tf;
	uint32_t		pa_domain;
	int			cont_tf;

	if (cuuid == NULL)
		return EINVAL;
	if (_dfs && _coh == NULL) {
		D_ERROR("Should pass a valid container handle pointer\n");
		return EINVAL;
	}

	if (attr != NULL && attr->da_props != NULL)
		prop = daos_prop_alloc(attr->da_props->dpp_nr + 2);
	else
		prop = daos_prop_alloc(2);
	if (prop == NULL)
		return ENOMEM;

	if (attr != NULL && attr->da_props != NULL) {
		rc = daos_prop_copy(prop, attr->da_props);
		if (rc) {
			D_ERROR("failed to copy properties "DF_RC"\n", DP_RC(rc));
			D_GOTO(err_prop, rc = daos_der2errno(rc));
		}
	}

	if (attr) {
		if (attr->da_oclass_id) {
			dattr.da_dir_oclass_id = attr->da_oclass_id;
			dattr.da_file_oclass_id = attr->da_oclass_id;
		}
		if (attr->da_file_oclass_id)
			dattr.da_file_oclass_id = attr->da_file_oclass_id;
		if (attr->da_dir_oclass_id)
			dattr.da_dir_oclass_id = attr->da_dir_oclass_id;

		/** check non default mode */
		if ((attr->da_mode & MODE_MASK) == DFS_RELAXED ||
		    (attr->da_mode & MODE_MASK) == DFS_BALANCED)
			dattr.da_mode = attr->da_mode;
		else
			dattr.da_mode = DFS_RELAXED;

		/** check non default chunk size */
		if (attr->da_chunk_size != 0)
			dattr.da_chunk_size = attr->da_chunk_size;
		else
			dattr.da_chunk_size = DFS_DEFAULT_CHUNK_SIZE;

		if (attr->da_hints[0] != 0) {
			strncpy(dattr.da_hints, attr->da_hints, DAOS_CONT_HINT_MAX_LEN);
			dattr.da_hints[DAOS_CONT_HINT_MAX_LEN - 1] = '\0';
		}
	} else {
		dattr.da_oclass_id = 0;
		dattr.da_dir_oclass_id = 0;
		dattr.da_file_oclass_id = 0;
		dattr.da_mode = DFS_RELAXED;
		dattr.da_chunk_size = DFS_DEFAULT_CHUNK_SIZE;
	}

	/** check if RF factor is set on property */
	dpe = daos_prop_entry_get(prop, DAOS_PROP_CO_REDUN_FAC);
	if (!dpe) {
		rc = dc_pool_get_redunc(poh);
		if (rc < 0)
			D_GOTO(err_prop, rc = daos_der2errno(rc));
		rf = rc;
	} else {
		rf = dpe->dpe_val;
	}

	/** verify object class redundancy */
	cont_tf = daos_cont_rf2allowedfailures(rf);
	if (cont_tf < 0)
		D_GOTO(err_prop, rc = EINVAL);

	if (dattr.da_file_oclass_id) {
		rc = daos_oclass_cid2allowedfailures(dattr.da_file_oclass_id, &cid_tf);
		if (rc) {
			D_ERROR("Invalid oclass OID\n");
			D_GOTO(err_prop, rc = daos_der2errno(rc));
		}
		if (cid_tf < cont_tf) {
			D_ERROR("File object class cannot tolerate RF failures\n");
			D_GOTO(err_prop, rc = EINVAL);
		}
	}
	if (dattr.da_dir_oclass_id) {
		rc = daos_oclass_cid2allowedfailures(dattr.da_dir_oclass_id, &cid_tf);
		if (rc) {
			D_ERROR("Invalid oclass OID\n");
			D_GOTO(err_prop, rc = daos_der2errno(rc));
		}
		if (cid_tf < cont_tf) {
			D_ERROR("Directory object class cannot tolerate RF failures\n");
			D_GOTO(err_prop, rc = EINVAL);
		}
	}

	pa_domain = daos_cont_prop2redunlvl(prop);

	/** check hints for SB and Root Dir */
	if (dattr.da_hints[0] != 0) {
		daos_oclass_hints_t file_hints;

		rc = get_oclass_hints(dattr.da_hints, &dir_oclass_hint, &file_hints, rf);
		if (rc)
			D_GOTO(err_prop, rc);
	}

	/* select oclass and generate SB OID */
	roots.cr_oids[0].lo = RESERVED_LO;
	roots.cr_oids[0].hi = SB_HI;
	rc = daos_obj_generate_oid_by_rf(poh, rf, &roots.cr_oids[0], 0, dattr.da_dir_oclass_id,
					 dir_oclass_hint, 0, pa_domain);
	if (rc) {
		D_ERROR("Failed to generate SB OID "DF_RC"\n", DP_RC(rc));
		D_GOTO(err_prop, rc = daos_der2errno(rc));
	}

	/* select oclass and generate ROOT OID */
	roots.cr_oids[1].lo = RESERVED_LO;
	roots.cr_oids[1].hi = ROOT_HI;
	rc = daos_obj_generate_oid_by_rf(poh, rf, &roots.cr_oids[1], 0, dattr.da_dir_oclass_id,
					 dir_oclass_hint, 0, pa_domain);
	if (rc) {
		D_ERROR("Failed to generate ROOT OID "DF_RC"\n", DP_RC(rc));
		D_GOTO(err_prop, rc = daos_der2errno(rc));
	}

	/* store SB & root OIDs as container property */
	roots.cr_oids[2] = roots.cr_oids[3] = DAOS_OBJ_NIL;
	prop->dpp_entries[prop->dpp_nr - 2].dpe_type = DAOS_PROP_CO_ROOTS;
	rc = daos_prop_entry_set_ptr(&prop->dpp_entries[prop->dpp_nr - 2], &roots, sizeof(roots));
	if (rc)
		D_GOTO(err_prop, rc = daos_der2errno(rc));

	prop->dpp_entries[prop->dpp_nr - 1].dpe_type = DAOS_PROP_CO_LAYOUT_TYPE;
	prop->dpp_entries[prop->dpp_nr - 1].dpe_val = DAOS_PROP_CO_LAYOUT_POSIX;

	rc = daos_cont_create(poh, cuuid, prop, NULL);
	if (rc) {
		D_ERROR("daos_cont_create() failed "DF_RC"\n", DP_RC(rc));
		D_GOTO(err_prop, rc = daos_der2errno(rc));
	}

	uuid_unparse(*cuuid, str);
	rc = daos_cont_open(poh, str, DAOS_COO_RW, &coh, &co_info, NULL);
	if (rc) {
		D_ERROR("daos_cont_open() failed "DF_RC"\n", DP_RC(rc));
		D_GOTO(err_destroy, rc = daos_der2errno(rc));
	}

	/** Create SB */
	rc = open_sb(coh, true, false, DAOS_OO_RW, roots.cr_oids[0], &dattr, &super_oh, NULL);
	if (rc)
		D_GOTO(err_close, rc);

	/** Add root object */
	entry.oid = roots.cr_oids[1];
	entry.mode = S_IFDIR | 0755;
	rc = clock_gettime(CLOCK_REALTIME, &now);
	if (rc)
		D_GOTO(err_super, rc = errno);
	entry.mtime = entry.ctime = now.tv_sec;
	entry.mtime_nano = entry.ctime_nano = now.tv_nsec;
	entry.uid = geteuid();
	entry.gid = getegid();

	/*
	 * Since we don't support daos cont create atomicity (2 or more cont
	 * creates on the same container will always succeed), we can get into a
	 * situation where the SB is created by one process, but return EEXIST
	 * on another. in this case we can just assume it is inserted, and
	 * continue.
	 */
	rc = insert_entry(DFS_LAYOUT_VERSION, super_oh, DAOS_TX_NONE, "/", 1, DAOS_COND_DKEY_INSERT,
			  &entry);
	if (rc && rc != EEXIST) {
		D_ERROR("Failed to insert root entry: %d (%s)\n", rc, strerror(rc));
		D_GOTO(err_super, rc);
	}

	rc = daos_obj_close(super_oh, NULL);
	if (rc) {
		D_ERROR("Failed to close SB object "DF_RC"\n", DP_RC(rc));
		D_GOTO(err_close, rc = daos_der2errno(rc));
	}

	if (_dfs) {
		/** Mount DFS on the container we just created */
		rc = dfs_mount(poh, coh, O_RDWR, &dfs);
		if (rc) {
			D_ERROR("dfs_mount() failed (%d)\n", rc);
			D_GOTO(err_close, rc);
		}
		dfs->layout_v = DFS_LAYOUT_VERSION;
		*_dfs = dfs;
	}

	if (_coh) {
		*_coh = coh;
	} else {
		rc = daos_cont_close(coh, NULL);
		if (rc) {
			D_ERROR("daos_cont_close() failed "DF_RC"\n", DP_RC(rc));
			D_GOTO(err_close, rc = daos_der2errno(rc));
		}
	}
	daos_prop_free(prop);
	return 0;
err_super:
	daos_obj_close(super_oh, NULL);
err_close:
	rc2 = daos_cont_close(coh, NULL);
	if (rc2)
		D_ERROR("daos_cont_close failed "DF_RC"\n", DP_RC(rc));
err_destroy:
	/*
	 * DAOS container create returns success even if container exists -
	 * DAOS-2700. So if the error here is EEXIST (it means we got it from
	 * the SB creation, so do not destroy the container, since another
	 * process might have created it.
	 */
	if (rc != EEXIST) {
		rc2 = daos_cont_destroy(poh, str, 1, NULL);
		if (rc2)
			D_ERROR("daos_cont_destroy failed "DF_RC"\n", DP_RC(rc));
	}
err_prop:
	daos_prop_free(prop);
	return rc;
}

int
dfs_cont_create_with_label(daos_handle_t poh, const char *label, dfs_attr_t *attr,
			   uuid_t *cuuid, daos_handle_t *coh, dfs_t **dfs)
{
	daos_prop_t		*label_prop;
	daos_prop_t		*merged_props = NULL;
	daos_prop_t		*orig = NULL;
	dfs_attr_t		local = {};
	int			rc;

	label_prop = daos_prop_alloc(1);
	if (label_prop == NULL)
		return ENOMEM;

	label_prop->dpp_entries[0].dpe_type = DAOS_PROP_CO_LABEL;
	rc = daos_prop_entry_set_str(&label_prop->dpp_entries[0], label, DAOS_PROP_LABEL_MAX_LEN);
	if (rc)
		D_GOTO(out_prop, rc = daos_der2errno(rc));

	if (attr == NULL)
		attr = &local;

	if (attr->da_props) {
		rc = daos_prop_merge2(attr->da_props, label_prop, &merged_props);
		if (rc != 0)
			D_GOTO(out_prop, rc = daos_der2errno(rc));
		orig = attr->da_props;
		attr->da_props = merged_props;
	} else {
		attr->da_props = label_prop;
	}

	if (cuuid == NULL) {
		uuid_t u;

		rc = dfs_cont_create(poh, &u, attr, coh, dfs);
	} else {
		rc = dfs_cont_create(poh, cuuid, attr, coh, dfs);
	}
	attr->da_props = orig;
	daos_prop_free(merged_props);
out_prop:
	daos_prop_free(label_prop);
	return rc;
}

int
dfs_connect(const char *pool, const char *sys, const char *cont, int flags, dfs_attr_t *attr,
	    dfs_t **_dfs)
{
	daos_handle_t		poh = {0};
	daos_handle_t		coh = {0};
	bool			pool_h_bump = false;
	bool			cont_h_bump = false;
	struct dfs_mnt_hdls	*pool_hdl = NULL;
	struct dfs_mnt_hdls	*cont_hdl = NULL;
	dfs_t			*dfs = NULL;
	int			amode, cmode;
	int			rc, rc2;

	if (_dfs == NULL || pool == NULL || cont == NULL)
		return EINVAL;

	if (!dfs_is_init()) {
		D_ERROR("dfs_init() must be called before dfs_connect() can be used\n");
		return EACCES;
	}

	amode = (flags & O_ACCMODE);

	pool_hdl = dfs_hdl_lookup(pool, DFS_H_POOL, NULL);
	if (pool_hdl == NULL) {
		/** Connect to pool */
		rc = daos_pool_connect(pool, sys, (amode == O_RDWR) ? DAOS_PC_RW : DAOS_PC_RO, &poh,
				       NULL, NULL);
		if (rc) {
			D_ERROR("Failed to connect to pool %s "DF_RC"\n", pool, DP_RC(rc));
			D_GOTO(err, rc = daos_der2errno(rc));
		}

		rc = dfs_hdl_insert(pool, DFS_H_POOL, NULL, &poh, &pool_hdl);
		if (rc)
			D_GOTO(err, rc);
	} else {
		poh.cookie = pool_hdl->handle.cookie;
	}
	pool_h_bump = true;

	cmode = (amode == O_RDWR) ? DAOS_COO_RW : DAOS_COO_RO;

	cont_hdl = dfs_hdl_lookup(cont, DFS_H_CONT, pool);
	if (cont_hdl == NULL) {
		rc = daos_cont_open(poh, cont, cmode, &coh, NULL, NULL);
		if (rc == -DER_NONEXIST && (flags & O_CREAT)) {
			uuid_t	cuuid;

			rc = dfs_cont_create_with_label(poh, cont, attr, &cuuid, &coh, &dfs);
			/** if someone got there first, re-open */
			if (rc == EEXIST) {
				rc = daos_cont_open(poh, cont, cmode, &coh, NULL, NULL);
				if (rc) {
					D_ERROR("Failed to open container %s "DF_RC"\n",
						cont, DP_RC(rc));
					D_GOTO(err, rc = daos_der2errno(rc));
				}
				goto mount;
			} else if (rc) {
				D_ERROR("Failed to create DFS container: %d\n", rc);
			}
		} else if (rc == 0) {
			int b;
mount:
			/*
			 * It could be that someone has created the container but has not created
			 * the SB yet (cont create and sb create are not transactional), so try a
			 * few times to mount with some backoff.
			 */
			for (b = 0; b < 7; b++) {
				rc = dfs_mount(poh, coh, amode, &dfs);
				if (rc == ENOENT)
					usleep(pow(10, b));
				else
					break;
			}
			if (rc) {
				D_ERROR("Failed to mount DFS: %d (%s)\n", rc, strerror(rc));
				D_GOTO(err, rc);
			}
		} else {
			D_ERROR("Failed to open container %s "DF_RC"\n", cont, DP_RC(rc));
			D_GOTO(err, rc = daos_der2errno(rc));
		}

		rc = dfs_hdl_insert(cont, DFS_H_CONT, pool, &coh, &cont_hdl);
		if (rc)
			D_GOTO(err, rc);
	} else {
		cont_h_bump = true;
		rc = dfs_mount(poh, cont_hdl->handle, amode, &dfs);
		if (rc) {
			D_ERROR("Failed to mount DFS: %d (%s)\n", rc, strerror(rc));
			D_GOTO(err, rc);
		}
	}

	dfs->pool_hdl = pool_hdl;
	dfs->cont_hdl = cont_hdl;
	dfs->mounted = DFS_MOUNT_ALL;
	*_dfs = dfs;

	return rc;

err:
	if (dfs) {
		rc2 = dfs_umount(dfs);
		if (rc2)
			D_ERROR("dfs_umount() Failed %d\n", rc2);
	}

	if (cont_h_bump) {
		dfs_hdl_release(cont_hdl);
	} else if (daos_handle_is_valid(coh)) {
		rc2 = daos_cont_close(coh, NULL);
		if (rc2)
			D_ERROR("daos_cont_close() Failed "DF_RC"\n", DP_RC(rc2));
	}

	if (pool_h_bump) {
		dfs_hdl_release(pool_hdl);
	} else if (daos_handle_is_valid(poh)) {
		rc2 = daos_pool_disconnect(poh, NULL);
		if (rc2)
			D_ERROR("daos_pool_disconnect() Failed "DF_RC"\n", DP_RC(rc2));
	}

	return rc;
}

int
dfs_disconnect(dfs_t *dfs)
{
	int		rc;

	if (dfs == NULL || !dfs->mounted)
		return EINVAL;
	if (dfs->mounted != DFS_MOUNT_ALL) {
		D_ERROR("DFS is not mounted with dfs_connect() or dfs_global2local_all()\n");
		return EINVAL;
	}

	dfs_hdl_release(dfs->cont_hdl);
	dfs_hdl_release(dfs->pool_hdl);

	/** set mounted flag MOUNT to be able to just umount */
	dfs->mounted = DFS_MOUNT;
	rc = dfs_umount(dfs);
	if (rc) {
		D_ERROR("dfs_umount() Failed %d\n", rc);
		D_GOTO(out, rc);
	}
out:
	return rc;
}

int
dfs_destroy(const char *pool, const char *sys, const char *cont, int force, daos_event_t *ev)
{
	daos_handle_t		poh = {0};
	bool			pool_h_bump = false;
	struct dfs_mnt_hdls	*pool_hdl = NULL;
	int			rc, rc2;

	if (pool == NULL || cont == NULL)
		return EINVAL;

	if (!dfs_is_init()) {
		D_ERROR("dfs_init() must be called before dfs_destroy() can be used\n");
		return EACCES;
	}

	pool_hdl = dfs_hdl_lookup(pool, DFS_H_POOL, NULL);
	if (pool_hdl == NULL) {
		/** Connect to pool */
		rc = daos_pool_connect(pool, sys, DAOS_PC_RW, &poh, NULL, NULL);
		if (rc) {
			D_ERROR("Failed to connect to pool %s "DF_RC"\n", pool, DP_RC(rc));
			D_GOTO(err, rc = daos_der2errno(rc));
		}

		rc = dfs_hdl_insert(pool, DFS_H_POOL, NULL, &poh, &pool_hdl);
		if (rc)
			D_GOTO(err, rc);
	} else {
		poh.cookie = pool_hdl->handle.cookie;
	}
	pool_h_bump = true;

	rc = dfs_hdl_cont_destroy(pool, cont, force);
	if (rc != 0 && rc != ENOENT) {
		D_ERROR("Failed to destroy cont hash entry: %d (%s)\n", rc, strerror(rc));
		return rc;
	}

	rc = daos_cont_destroy(poh, cont, force, ev);
	if (rc) {
		D_ERROR("Failed to destroy container %s "DF_RC"\n", cont, DP_RC(rc));
		D_GOTO(err, rc = daos_der2errno(rc));
	}
	dfs_hdl_release(pool_hdl);
	return rc;
err:
	if (pool_h_bump) {
		dfs_hdl_release(pool_hdl);
	} else if (daos_handle_is_valid(poh)) {
		rc2 = daos_pool_disconnect(poh, NULL);
		if (rc2)
			D_ERROR("daos_pool_disconnect() Failed "DF_RC"\n", DP_RC(rc2));
	}
	return rc;
}

int
dfs_mount(daos_handle_t poh, daos_handle_t coh, int flags, dfs_t **_dfs)
{
	dfs_t				*dfs;
	daos_prop_t			*prop;
	struct daos_prop_entry		*entry;
	struct daos_prop_co_roots	*roots;
	struct dfs_entry		root_dir;
	int				amode, omode;
	int				rc;
	int				i;
	uint32_t			props[] = {DAOS_PROP_CO_LAYOUT_TYPE,
						   DAOS_PROP_CO_ROOTS,
						   DAOS_PROP_CO_REDUN_FAC};
	const int			num_props = ARRAY_SIZE(props);

	if (_dfs == NULL)
		return EINVAL;

	amode = (flags & O_ACCMODE);
	omode = get_daos_obj_mode(flags);
	if (omode == -1)
		return EINVAL;

	prop = daos_prop_alloc(num_props);
	if (prop == NULL)
		return ENOMEM;

	for (i = 0; i < num_props; i++)
		prop->dpp_entries[i].dpe_type = props[i];

	rc = daos_cont_query(coh, NULL, prop, NULL);
	if (rc) {
		D_ERROR("daos_cont_query() failed, "DF_RC"\n", DP_RC(rc));
		D_GOTO(err_prop, rc = daos_der2errno(rc));
	}

	entry = daos_prop_entry_get(prop, DAOS_PROP_CO_LAYOUT_TYPE);
	if (entry == NULL || entry->dpe_val != DAOS_PROP_CO_LAYOUT_POSIX) {
		D_ERROR("container is not of type POSIX\n");
		D_GOTO(err_prop, rc = EINVAL);
	}

	D_ALLOC_PTR(dfs);
	if (dfs == NULL)
		D_GOTO(err_prop, rc = ENOMEM);

	dfs->poh = poh;
	dfs->coh = coh;
	dfs->amode = amode;

	rc = D_MUTEX_INIT(&dfs->lock, NULL);
	if (rc != 0)
		D_GOTO(err_dfs, rc = daos_der2errno(rc));

	entry = daos_prop_entry_get(prop, DAOS_PROP_CO_ROOTS);
	D_ASSERT(entry != NULL);
	roots = (struct daos_prop_co_roots *)entry->dpe_val_ptr;
	if (daos_obj_id_is_nil(roots->cr_oids[0]) ||
	    daos_obj_id_is_nil(roots->cr_oids[1])) {
		D_ERROR("Invalid superblock or root object ID\n");
		D_GOTO(err_dfs, rc = EIO);
	}

	dfs->super_oid = roots->cr_oids[0];
	dfs->root.oid = roots->cr_oids[1];
	dfs->root.parent_oid = dfs->super_oid;

	/** Verify SB */
	rc = open_sb(coh, false, false, omode, dfs->super_oid, &dfs->attr, &dfs->super_oh,
		     &dfs->layout_v);
	if (rc)
		D_GOTO(err_dfs, rc);

	/** set oid hints for files and dirs */
	if (dfs->attr.da_hints[0] != 0) {
		entry = daos_prop_entry_get(prop, DAOS_PROP_CO_REDUN_FAC);
		D_ASSERT(entry != NULL);

		rc = get_oclass_hints(dfs->attr.da_hints, &dfs->dir_oclass_hint,
				      &dfs->file_oclass_hint, entry->dpe_val);
		if (rc)
			D_GOTO(err_prop, rc);
	}

	/*
	 * If container was created with balanced mode, only balanced mode
	 * mounting should be allowed.
	 */
	if ((dfs->attr.da_mode & MODE_MASK) == DFS_BALANCED) {
		if ((flags & MODE_MASK) != DFS_BALANCED) {
			D_ERROR("Can't use non-balanced mount flag on a POSIX"
				" container created with balanced mode.\n");
			D_GOTO(err_super, rc = EPERM);
		}
		dfs->use_dtx = true;
		D_DEBUG(DB_ALL, "DFS mount in Balanced mode.\n");
	} else {
		if ((dfs->attr.da_mode & MODE_MASK) != DFS_RELAXED) {
			D_ERROR("Invalid DFS mode in Superblock\n");
			D_GOTO(err_super, rc = EINVAL);
		}

		if ((flags & MODE_MASK) == DFS_BALANCED) {
			dfs->use_dtx = true;
			D_DEBUG(DB_ALL, "DFS mount in Balanced mode.\n");
		} else {
			dfs->use_dtx = false;
			D_DEBUG(DB_ALL, "DFS mount in Relaxed mode.\n");
		}
	}

	/*
	 * For convenience, keep env variable option for now that overrides the
	 * default input setting, only if container was created with relaxed
	 * mode.
	 */
	if ((dfs->attr.da_mode & MODE_MASK) == DFS_RELAXED)
		d_getenv_bool("DFS_USE_DTX", &dfs->use_dtx);

	/** Check if super object has the root entry */
	strcpy(dfs->root.name, "/");
	rc = open_dir(dfs, NULL, amode, flags, &root_dir, 1, &dfs->root);
	if (rc) {
		D_ERROR("Failed to open root object: %d (%s)\n", rc, strerror(rc));
		D_GOTO(err_super, rc);
	}

	dfs->root_stbuf.st_nlink = 1;
	dfs->root_stbuf.st_size = sizeof(root_dir);
	dfs->root_stbuf.st_mode = dfs->root.mode;
	dfs->root_stbuf.st_uid = root_dir.uid;
	dfs->root_stbuf.st_gid = root_dir.gid;
	dfs->root_stbuf.st_mtim.tv_sec = root_dir.mtime;
	dfs->root_stbuf.st_mtim.tv_nsec = root_dir.mtime_nano;
	dfs->root_stbuf.st_ctim.tv_sec = root_dir.ctime;
	dfs->root_stbuf.st_ctim.tv_nsec = root_dir.ctime_nano;
	if (tspec_gt(dfs->root_stbuf.st_ctim, dfs->root_stbuf.st_mtim)) {
		dfs->root_stbuf.st_atim.tv_sec = root_dir.ctime;
		dfs->root_stbuf.st_atim.tv_nsec = root_dir.ctime_nano;
	} else {
		dfs->root_stbuf.st_atim.tv_sec = root_dir.mtime;
		dfs->root_stbuf.st_atim.tv_nsec = root_dir.mtime_nano;
	}

	/** if RW, allocate an OID for the namespace */
	if (amode == O_RDWR) {
		rc = daos_cont_alloc_oids(coh, 1, &dfs->oid.lo, NULL);
		if (rc) {
			D_ERROR("daos_cont_alloc_oids() Failed, "DF_RC"\n",
				DP_RC(rc));
			D_GOTO(err_root, rc = daos_der2errno(rc));
		}

		/*
		 * if this is the first time we allocate on this container,
		 * account 0 for SB, 1 for root obj.
		 */
		if (dfs->oid.lo == RESERVED_LO)
			dfs->oid.hi = ROOT_HI + 1;
		else
			dfs->oid.hi = 0;
	}

	dfs->mounted = DFS_MOUNT;
	*_dfs = dfs;
	daos_prop_free(prop);
	return rc;

err_root:
	daos_obj_close(dfs->root.oh, NULL);
err_super:
	daos_obj_close(dfs->super_oh, NULL);
err_dfs:
	D_FREE(dfs);
err_prop:
	daos_prop_free(prop);
	return rc;
}

int
dfs_umount(dfs_t *dfs)
{
	if (dfs == NULL || !dfs->mounted)
		return EINVAL;
	if (dfs->mounted != DFS_MOUNT) {
		D_ERROR("DFS is not mounted with dfs_mount() or dfs_global2local()\n");
		return EINVAL;
	}

	D_MUTEX_LOCK(&dfs->lock);
	if (dfs->poh_refcount != 0) {
		D_ERROR("Pool open handle refcount not 0\n");
		D_MUTEX_UNLOCK(&dfs->lock);
		return EBUSY;
	}
	if (dfs->coh_refcount != 0) {
		D_ERROR("Cont open handle refcount not 0\n");
		D_MUTEX_UNLOCK(&dfs->lock);
		return EBUSY;
	}
	D_MUTEX_UNLOCK(&dfs->lock);

	daos_obj_close(dfs->root.oh, NULL);
	daos_obj_close(dfs->super_oh, NULL);

	D_FREE(dfs->prefix);
	D_MUTEX_DESTROY(&dfs->lock);
	D_FREE(dfs);

	return 0;
}

int
dfs_pool_get(dfs_t *dfs, daos_handle_t *poh)
{
	if (dfs == NULL || !dfs->mounted)
		return EINVAL;

	D_MUTEX_LOCK(&dfs->lock);
	dfs->poh_refcount++;
	D_MUTEX_UNLOCK(&dfs->lock);

	*poh = dfs->poh;
	return 0;
}

int
dfs_pool_put(dfs_t *dfs, daos_handle_t poh)
{
	if (dfs == NULL || !dfs->mounted)
		return EINVAL;
	if (poh.cookie != dfs->poh.cookie) {
		D_ERROR("Pool handle is not the same as the DFS Mount handle\n");
		return EINVAL;
	}

	D_MUTEX_LOCK(&dfs->lock);
	if (dfs->poh_refcount <= 0) {
		D_ERROR("Invalid pool handle refcount\n");
		D_MUTEX_UNLOCK(&dfs->lock);
		return EINVAL;
	}
	dfs->poh_refcount--;
	D_MUTEX_UNLOCK(&dfs->lock);

	return 0;
}

int
dfs_cont_get(dfs_t *dfs, daos_handle_t *coh)
{
	if (dfs == NULL || !dfs->mounted)
		return EINVAL;

	D_MUTEX_LOCK(&dfs->lock);
	dfs->coh_refcount++;
	D_MUTEX_UNLOCK(&dfs->lock);

	*coh = dfs->coh;
	return 0;
}

int
dfs_cont_put(dfs_t *dfs, daos_handle_t coh)
{
	if (dfs == NULL || !dfs->mounted)
		return EINVAL;
	if (coh.cookie != dfs->coh.cookie) {
		D_ERROR("Cont handle is not the same as the DFS Mount handle\n");
		return EINVAL;
	}

	D_MUTEX_LOCK(&dfs->lock);
	if (dfs->coh_refcount <= 0) {
		D_ERROR("Invalid cont handle refcount\n");
		D_MUTEX_UNLOCK(&dfs->lock);
		return EINVAL;
	}
	dfs->coh_refcount--;
	D_MUTEX_UNLOCK(&dfs->lock);

	return 0;
}

int
dfs_query(dfs_t *dfs, dfs_attr_t *attr)
{
	if (dfs == NULL || !dfs->mounted || attr == NULL)
		return EINVAL;

	memcpy(attr, &dfs->attr, sizeof(dfs_attr_t));

	return 0;
}

/* Structure of global buffer for dfs */
struct dfs_glob {
	uint32_t		magic;
	bool			use_dtx;
	dfs_layout_ver_t	layout_v;
	int32_t			amode;
	uid_t			uid;
	gid_t			gid;
	uint64_t		id;
	daos_size_t		chunk_size;
	daos_oclass_id_t	oclass;
	daos_oclass_id_t	dir_oclass;
	daos_oclass_id_t	file_oclass;
	uuid_t			cont_uuid;
	uuid_t			coh_uuid;
	daos_obj_id_t		super_oid;
	daos_obj_id_t		root_oid;
};

static inline void
swap_dfs_glob(struct dfs_glob *dfs_params)
{
	D_ASSERT(dfs_params != NULL);

	D_SWAP32S(&dfs_params->magic);
	D_SWAP32S(&dfs_params->use_dtx);
	D_SWAP16S(&dfs_params->layout_v);
	D_SWAP32S(&dfs_params->amode);
	D_SWAP32S(&dfs_params->uid);
	D_SWAP32S(&dfs_params->gid);
	D_SWAP64S(&dfs_params->id);
	D_SWAP64S(&dfs_params->chunk_size);
	D_SWAP16S(&dfs_params->oclass);
	D_SWAP16S(&dfs_params->dir_oclass);
	D_SWAP16S(&dfs_params->file_oclass);
	/* skip cont_uuid */
	/* skip coh_uuid */
}

static inline daos_size_t
dfs_glob_buf_size()
{
	return sizeof(struct dfs_glob);
}

int
dfs_local2global(dfs_t *dfs, d_iov_t *glob)
{
	struct dfs_glob		*dfs_params;
	uuid_t			coh_uuid;
	uuid_t			cont_uuid;
	daos_size_t		glob_buf_size;
	int			rc = 0;

	if (dfs == NULL || !dfs->mounted)
		return EINVAL;

	if (glob == NULL) {
		D_ERROR("Invalid parameter, NULL glob pointer.\n");
		return EINVAL;
	}

	if (glob->iov_buf != NULL && (glob->iov_buf_len == 0 ||
				      glob->iov_buf_len < glob->iov_len)) {
		D_ERROR("Invalid parameter of glob, iov_buf %p, iov_buf_len "
			""DF_U64", iov_len "DF_U64".\n", glob->iov_buf,
			glob->iov_buf_len, glob->iov_len);
		return EINVAL;
	}

	glob_buf_size = dfs_glob_buf_size();

	if (glob->iov_buf == NULL) {
		glob->iov_buf_len = glob_buf_size;
		return 0;
	}

	rc = dc_cont_hdl2uuid(dfs->coh, &coh_uuid, &cont_uuid);
	if (rc != 0)
		return daos_der2errno(rc);

	if (glob->iov_buf_len < glob_buf_size) {
		D_DEBUG(DB_ANY, "Larger glob buffer needed ("DF_U64" bytes "
			"provided, "DF_U64" required).\n", glob->iov_buf_len,
			glob_buf_size);
		glob->iov_buf_len = glob_buf_size;
		return ENOBUFS;
	}
	glob->iov_len = glob_buf_size;

	/* init global handle */
	dfs_params = (struct dfs_glob *)glob->iov_buf;
	dfs_params->magic	= DFS_GLOB_MAGIC;
	dfs_params->use_dtx	= dfs->use_dtx;
	dfs_params->layout_v	= dfs->layout_v;
	dfs_params->amode	= dfs->amode;
	dfs_params->super_oid	= dfs->super_oid;
	dfs_params->root_oid	= dfs->root.oid;
	dfs_params->uid		= dfs->uid;
	dfs_params->gid		= dfs->gid;
	dfs_params->id		= dfs->attr.da_id;
	dfs_params->chunk_size	= dfs->attr.da_chunk_size;
	dfs_params->oclass	= dfs->attr.da_oclass_id;
	dfs_params->dir_oclass	= dfs->attr.da_dir_oclass_id;
	dfs_params->file_oclass	= dfs->attr.da_file_oclass_id;
	uuid_copy(dfs_params->coh_uuid, coh_uuid);
	uuid_copy(dfs_params->cont_uuid, cont_uuid);

	return 0;
}

int
dfs_global2local(daos_handle_t poh, daos_handle_t coh, int flags, d_iov_t glob,
		 dfs_t **_dfs)
{
	dfs_t		*dfs;
	struct dfs_glob	*dfs_params;
	int		obj_mode;
	uuid_t		coh_uuid;
	uuid_t		cont_uuid;
	int		rc = 0;

	if (_dfs == NULL)
		return EINVAL;

	if (glob.iov_buf == NULL || glob.iov_buf_len < glob.iov_len ||
	    glob.iov_len != dfs_glob_buf_size()) {
		D_ERROR("Invalid parameter of glob, iov_buf %p, "
			"iov_buf_len "DF_U64", iov_len "DF_U64".\n",
			glob.iov_buf, glob.iov_buf_len, glob.iov_len);
		return EINVAL;
	}

	dfs_params = (struct dfs_glob *)glob.iov_buf;
	if (dfs_params->magic == D_SWAP32(DFS_GLOB_MAGIC)) {
		swap_dfs_glob(dfs_params);
		D_ASSERT(dfs_params->magic == DFS_GLOB_MAGIC);

	} else if (dfs_params->magic != DFS_GLOB_MAGIC) {
		D_ERROR("Bad magic value: %#x.\n", dfs_params->magic);
		return EINVAL;
	}

	D_ASSERT(dfs_params != NULL);

	/** Check container uuid mismatch */
	rc = dc_cont_hdl2uuid(coh, &coh_uuid, &cont_uuid);
	if (rc != 0)
		return daos_der2errno(rc);
	if (uuid_compare(cont_uuid, dfs_params->cont_uuid) != 0) {
		D_ERROR("Container uuid mismatch, in coh: "DF_UUID", "
			"in dfs_params:" DF_UUID"\n", DP_UUID(cont_uuid),
			DP_UUID(dfs_params->cont_uuid));
		return EINVAL;
	}

	/** Create the DFS handle with no RPCs */
	D_ALLOC_PTR(dfs);
	if (dfs == NULL)
		return ENOMEM;

	dfs->poh = poh;
	dfs->coh = coh;
	dfs->use_dtx = dfs_params->use_dtx;
	dfs->layout_v = dfs_params->layout_v;
	dfs->amode = (flags == 0) ? dfs_params->amode : (flags & O_ACCMODE);
	dfs->uid = dfs_params->uid;
	dfs->gid = dfs_params->gid;
	dfs->attr.da_id = dfs_params->id;
	dfs->attr.da_chunk_size = dfs_params->chunk_size;
	dfs->attr.da_oclass_id = dfs_params->oclass;
	dfs->attr.da_dir_oclass_id = dfs_params->dir_oclass;
	dfs->attr.da_file_oclass_id = dfs_params->file_oclass;

	dfs->super_oid = dfs_params->super_oid;
	dfs->root.oid = dfs_params->root_oid;
	dfs->root.parent_oid = dfs->super_oid;
	if (daos_obj_id_is_nil(dfs->super_oid) ||
	    daos_obj_id_is_nil(dfs->root.oid)) {
		D_ERROR("Invalid superblock or root object ID\n");
		D_FREE(dfs);
		return EIO;
	}

	/** allocate a new oid on the next file or dir creation */
	dfs->oid.lo = 0;
	dfs->oid.hi = MAX_OID_HI;

	rc = D_MUTEX_INIT(&dfs->lock, NULL);
	if (rc != 0) {
		D_FREE(dfs);
		return daos_der2errno(rc);
	}

	/** Open SB object */
	rc = daos_obj_open(coh, dfs->super_oid, DAOS_OO_RO,
			   &dfs->super_oh, NULL);
	if (rc) {
		D_ERROR("daos_obj_open() failed, "DF_RC"\n", DP_RC(rc));
		D_GOTO(err_dfs, rc = daos_der2errno(rc));
	}

	/* Open Root Object */
	strcpy(dfs->root.name, "/");
	dfs->root.mode = S_IFDIR | 0755;

	obj_mode = get_daos_obj_mode(flags ? flags : dfs_params->amode);
	rc = daos_obj_open(coh, dfs->root.oid, obj_mode, &dfs->root.oh, NULL);
	if (rc) {
		D_ERROR("daos_obj_open() failed, "DF_RC"\n", DP_RC(rc));
		daos_obj_close(dfs->super_oh, NULL);
		D_GOTO(err_dfs, rc = daos_der2errno(rc));
	}

	dfs->mounted = DFS_MOUNT;
	*_dfs = dfs;

	return rc;
err_dfs:
	D_MUTEX_DESTROY(&dfs->lock);
	D_FREE(dfs);
	return rc;
}

int
dfs_local2global_all(dfs_t *dfs, d_iov_t *glob)
{
	d_iov_t		pool_iov = { NULL, 0, 0 };
	d_iov_t		cont_iov = { NULL, 0, 0 };
	d_iov_t		dfs_iov = { NULL, 0, 0 };
	daos_size_t	pool_len, cont_len, total_size;
	char		*ptr;
	int		rc = 0;

	if (dfs == NULL || !dfs->mounted)
		return EINVAL;
	if (glob == NULL) {
		D_ERROR("Invalid parameter, NULL glob pointer.\n");
		return EINVAL;
	}
	if (glob->iov_buf != NULL && (glob->iov_buf_len == 0)) {
		D_ERROR("Invalid parameter of glob, iov_buf %p, iov_buf_len"
			""DF_U64", iov_len "DF_U64".\n", glob->iov_buf,
			glob->iov_buf_len, glob->iov_len);
		return EINVAL;
	}

	rc = daos_pool_local2global(dfs->poh, &pool_iov);
	if (rc)
		return daos_der2errno(rc);

	rc = daos_cont_local2global(dfs->coh, &cont_iov);
	if (rc)
		return daos_der2errno(rc);

	rc = dfs_local2global(dfs, &dfs_iov);
	if (rc)
		return rc;

	pool_len = strlen(dfs->pool_hdl->value) + 1;
	cont_len = strlen(dfs->cont_hdl->value) + 1;
	total_size = pool_iov.iov_buf_len + cont_iov.iov_buf_len + dfs_iov.iov_buf_len + pool_len +
		cont_len + sizeof(daos_size_t) * 5;

	if (glob->iov_buf == NULL) {
		glob->iov_buf_len = total_size;
		return 0;
	}

	ptr = glob->iov_buf;

	/** format: pool label - pool hdl size - pool hdl */
	strcpy(ptr, dfs->pool_hdl->value);
	ptr += pool_len;
	*((daos_size_t *) ptr) = pool_iov.iov_buf_len;
	ptr += sizeof(daos_size_t);
	pool_iov.iov_buf = ptr;
	pool_iov.iov_len = pool_iov.iov_buf_len;
	rc = daos_pool_local2global(dfs->poh, &pool_iov);
	if (rc)
		return daos_der2errno(rc);
	ptr += pool_iov.iov_buf_len;

	/** format: cont label - cont hdl size - cont hdl */
	strcpy(ptr, dfs->cont_hdl->value);
	ptr += cont_len;
	*((daos_size_t *) ptr) = cont_iov.iov_buf_len;
	ptr += sizeof(daos_size_t);
	cont_iov.iov_buf = ptr;
	cont_iov.iov_len = cont_iov.iov_buf_len;
	rc = daos_cont_local2global(dfs->coh, &cont_iov);
	if (rc)
		return daos_der2errno(rc);
	ptr += cont_iov.iov_buf_len;

	*((daos_size_t *) ptr) = dfs_iov.iov_buf_len;
	ptr += sizeof(daos_size_t);
	dfs_iov.iov_buf = ptr;
	dfs_iov.iov_len = dfs_iov.iov_buf_len;
	rc = dfs_local2global(dfs, &dfs_iov);
	if (rc)
		return rc;

	return 0;
}

int
dfs_global2local_all(int flags, d_iov_t glob, dfs_t **_dfs)
{
	char			*ptr;
	d_iov_t			pool_iov = { NULL, 0, 0 };
	d_iov_t			cont_iov = { NULL, 0, 0 };
	d_iov_t			dfs_iov = { NULL, 0, 0 };
	daos_size_t		pool_len, cont_len;
	char			pool[DAOS_PROP_LABEL_MAX_LEN + 1];
	char			cont[DAOS_PROP_LABEL_MAX_LEN + 1];
	bool			pool_h_bump = false;
	bool			cont_h_bump = false;
	struct dfs_mnt_hdls	*pool_hdl = NULL;
	struct dfs_mnt_hdls	*cont_hdl = NULL;
	daos_handle_t		poh = {0};
	daos_handle_t		coh = {0};
	dfs_t			*dfs = NULL;
	int			rc, rc2;

	if (_dfs == NULL)
		return EINVAL;
	if (!dfs_is_init()) {
		D_ERROR("dfs_init() must be called before dfs_global2local_all() can be used\n");
		return EACCES;
	}
	if (glob.iov_buf == NULL || glob.iov_buf_len < glob.iov_len) {
		D_ERROR("Invalid parameter of glob, iov_buf %p, "
			"iov_buf_len "DF_U64", iov_len "DF_U64".\n",
			glob.iov_buf, glob.iov_buf_len, glob.iov_len);
		return EINVAL;
	}

	ptr = (char *)glob.iov_buf;

	strncpy(pool, ptr, DAOS_PROP_LABEL_MAX_LEN + 1);
	pool[DAOS_PROP_LABEL_MAX_LEN] = 0;
	pool_len = strlen(pool) + 1;
	ptr += pool_len;
	pool_iov.iov_buf_len = *((daos_size_t *) ptr);
	ptr += sizeof(daos_size_t);
	pool_iov.iov_buf = ptr;
	pool_iov.iov_len = pool_iov.iov_buf_len;
	rc = daos_pool_global2local(pool_iov, &poh);
	if (rc)
		D_GOTO(err, rc = daos_der2errno(rc));
	ptr += pool_iov.iov_buf_len;
	rc = dfs_hdl_insert(pool, DFS_H_POOL, NULL, &poh, &pool_hdl);
	if (rc)
		D_GOTO(err, rc);
	pool_h_bump = true;

	strncpy(cont, ptr, DAOS_PROP_LABEL_MAX_LEN + 1);
	cont[DAOS_PROP_LABEL_MAX_LEN] = 0;
	cont_len = strlen(cont) + 1;
	ptr += cont_len;
	cont_iov.iov_buf_len = *((daos_size_t *) ptr);
	ptr += sizeof(daos_size_t);
	cont_iov.iov_buf = ptr;
	cont_iov.iov_len = cont_iov.iov_buf_len;
	rc = daos_cont_global2local(poh, cont_iov, &coh);
	if (rc)
		D_GOTO(err, rc = daos_der2errno(rc));
	ptr += cont_iov.iov_buf_len;
	rc = dfs_hdl_insert(cont, DFS_H_CONT, pool, &coh, &cont_hdl);
	if (rc)
		D_GOTO(err, rc);
	cont_h_bump = true;

	dfs_iov.iov_buf_len = *((daos_size_t *) ptr);
	ptr += sizeof(daos_size_t);
	dfs_iov.iov_buf = ptr;
	dfs_iov.iov_len = dfs_iov.iov_buf_len;
	rc = dfs_global2local(poh, coh, flags, dfs_iov, &dfs);
	if (rc)
		D_GOTO(err, rc);

	dfs->pool_hdl = pool_hdl;
	dfs->cont_hdl = cont_hdl;
	dfs->mounted = DFS_MOUNT_ALL;

	*_dfs = dfs;

	return rc;

err:
	if (dfs) {
		rc2 = dfs_umount(dfs);
		if (rc2)
			D_ERROR("dfs_umount() Failed %d\n", rc2);
	}

	if (cont_h_bump) {
		dfs_hdl_release(cont_hdl);
	} else if (daos_handle_is_valid(coh)) {
		rc2 = daos_cont_close(coh, NULL);
		if (rc2)
			D_ERROR("daos_cont_close() Failed "DF_RC"\n", DP_RC(rc2));
	}

	if (pool_h_bump) {
		dfs_hdl_release(pool_hdl);
	} else if (daos_handle_is_valid(poh)) {
		rc2 = daos_pool_disconnect(poh, NULL);
		if (rc2)
			D_ERROR("daos_pool_disconnect() Failed "DF_RC"\n", DP_RC(rc2));
	}

	return rc;
}

int
dfs_set_prefix(dfs_t *dfs, const char *prefix)
{
	if (dfs == NULL || !dfs->mounted)
		return EINVAL;

	if (prefix == NULL) {
		D_FREE(dfs->prefix);
		return 0;
	}

	if (prefix[0] != '/' || strnlen(prefix, DFS_MAX_PATH) > DFS_MAX_PATH - 1)
		return EINVAL;

	D_STRNDUP(dfs->prefix, prefix, DFS_MAX_PATH - 1);
	if (dfs->prefix == NULL)
		return ENOMEM;

	dfs->prefix_len = strlen(dfs->prefix);
	if (dfs->prefix[dfs->prefix_len - 1] == '/')
		dfs->prefix_len--;

	return 0;
}

int
dfs_get_file_oh(dfs_obj_t *obj, daos_handle_t *oh)
{
	if (obj == NULL || !S_ISREG(obj->mode))
		return EINVAL;
	if (oh == NULL)
		return EINVAL;

	oh->cookie = obj->oh.cookie;
	return 0;
}

int
dfs_get_chunk_size(dfs_obj_t *obj, daos_size_t *chunk_size)
{
	daos_size_t	cell_size;
	int		rc;

	if (obj == NULL || !S_ISREG(obj->mode))
		return EINVAL;
	if (chunk_size == NULL)
		return EINVAL;

	rc = daos_array_get_attr(obj->oh, chunk_size, &cell_size);
	if (rc)
		return daos_der2errno(rc);

	D_ASSERT(cell_size == 1);
	return 0;
}

void
dfs_obj_copy_attr(dfs_obj_t *obj, dfs_obj_t *src_obj)
{
	if (S_ISDIR(obj->mode)) {
		obj->d.oclass = src_obj->d.oclass;
		obj->d.chunk_size = src_obj->d.chunk_size;
	}
}

int
dfs_obj_get_info(dfs_t *dfs, dfs_obj_t *obj, dfs_obj_info_t *info)
{
	int	rc;

	if (obj == NULL || info == NULL)
		return EINVAL;

	switch (obj->mode & S_IFMT) {
	case S_IFDIR:
		if (obj->d.oclass) {
			info->doi_oclass_id = obj->d.oclass;
		} else if (dfs->attr.da_dir_oclass_id) {
			info->doi_oclass_id = dfs->attr.da_dir_oclass_id;
		} else {
			rc = daos_obj_get_oclass(dfs->coh, 0, 0, 0, &info->doi_oclass_id);
			if (rc) {
				D_ERROR("daos_obj_get_oclass() failed "DF_RC"\n", DP_RC(rc));
				return daos_der2errno(rc);
			}
		}

		if (obj->d.chunk_size)
			info->doi_chunk_size = obj->d.chunk_size;
		else if (dfs->attr.da_chunk_size)
			info->doi_chunk_size = dfs->attr.da_chunk_size;
		else
			info->doi_chunk_size =  DFS_DEFAULT_CHUNK_SIZE;
		break;
	case S_IFREG:
	{
		daos_size_t cell_size;

		rc = daos_array_get_attr(obj->oh, &info->doi_chunk_size, &cell_size);
		if (rc)
			return daos_der2errno(rc);

		info->doi_oclass_id = daos_obj_id2class(obj->oid);
		break;
	}
	case S_IFLNK:
		info->doi_oclass_id = 0;
		info->doi_chunk_size = 0;
		break;
	default:
		D_ERROR("Invalid entry type (not a dir, file, symlink).\n");
		return EINVAL;
	}

	return 0;
}

int
dfs_obj_set_oclass(dfs_t *dfs, dfs_obj_t *obj, int flags, daos_oclass_id_t cid)
{
	daos_handle_t		oh;
	d_sg_list_t		sgl;
	d_iov_t			sg_iov;
	daos_iod_t		iod;
	daos_recx_t		recx;
	daos_key_t		dkey;
	int			rc;

	if (dfs == NULL || !dfs->mounted)
		return EINVAL;
	if (obj == NULL)
		return EINVAL;
	if (!S_ISDIR(obj->mode))
		return ENOTSUP;
	/** 0 is default, allow setting it */
	if (cid != 0 && !daos_oclass_is_valid(cid))
		return EINVAL;
	if (cid == 0)
		cid = dfs->attr.da_dir_oclass_id;

	/** Open parent object and fetch entry of obj from it */
	rc = daos_obj_open(dfs->coh, obj->parent_oid, DAOS_OO_RW, &oh, NULL);
	if (rc)
		return daos_der2errno(rc);

	/** set dkey as the entry name */
	d_iov_set(&dkey, (void *)obj->name, strlen(obj->name));

	/** set akey as the inode name */
	d_iov_set(&iod.iod_name, INODE_AKEY_NAME, sizeof(INODE_AKEY_NAME) - 1);
	iod.iod_nr	= 1;
	iod.iod_size	= 1;
	recx.rx_idx	= OCLASS_IDX;
	recx.rx_nr      = sizeof(daos_oclass_id_t);
	iod.iod_recxs	= &recx;
	iod.iod_type	= DAOS_IOD_ARRAY;

	/** set sgl for update */
	d_iov_set(&sg_iov, &cid, sizeof(daos_oclass_id_t));
	sgl.sg_nr	= 1;
	sgl.sg_nr_out	= 0;
	sgl.sg_iovs	= &sg_iov;

	rc = daos_obj_update(oh, DAOS_TX_NONE, DAOS_COND_DKEY_UPDATE, &dkey, 1,
			     &iod, &sgl, NULL);
	if (rc) {
		D_ERROR("Failed to update object class: " DF_RC "\n", DP_RC(rc));
		D_GOTO(out, rc = daos_der2errno(rc));
	}

	/** if this is root obj, we need to update the cached handle oclass */
	if (daos_oid_cmp(obj->oid, dfs->root.oid) == 0)
		dfs->root.d.oclass = cid;

out:
	daos_obj_close(oh, NULL);
	return rc;
}

int
set_chunk_size(dfs_t *dfs, dfs_obj_t *obj, daos_size_t csize)
{
	daos_handle_t		oh;
	d_sg_list_t		sgl;
	d_iov_t			sg_iov;
	daos_iod_t		iod;
	daos_recx_t		recx;
	daos_key_t		dkey;
	int			rc;

	/** Open parent object and fetch entry of obj from it */
	rc = daos_obj_open(dfs->coh, obj->parent_oid, DAOS_OO_RW, &oh, NULL);
	if (rc)
		return daos_der2errno(rc);

	/** set dkey as the entry name */
	d_iov_set(&dkey, (void *)obj->name, strlen(obj->name));

	/** set akey as the inode name */
	d_iov_set(&iod.iod_name, INODE_AKEY_NAME, sizeof(INODE_AKEY_NAME) - 1);
	iod.iod_nr	= 1;
	iod.iod_size	= 1;
	recx.rx_idx	= CSIZE_IDX;
	recx.rx_nr      = sizeof(daos_size_t);
	iod.iod_recxs	= &recx;
	iod.iod_type	= DAOS_IOD_ARRAY;

	/** set sgl for update */
	d_iov_set(&sg_iov, &csize, sizeof(daos_size_t));
	sgl.sg_nr	= 1;
	sgl.sg_nr_out	= 0;
	sgl.sg_iovs	= &sg_iov;

	rc = daos_obj_update(oh, DAOS_TX_NONE, DAOS_COND_DKEY_UPDATE, &dkey, 1,
			     &iod, &sgl, NULL);
	if (rc) {
		D_ERROR("Failed to update chunk size: " DF_RC "\n", DP_RC(rc));
		D_GOTO(out, rc = daos_der2errno(rc));
	}

out:
	daos_obj_close(oh, NULL);
	return rc;
}

int
dfs_obj_set_chunk_size(dfs_t *dfs, dfs_obj_t *obj, int flags, daos_size_t csize)
{
	int	rc;

	if (obj == NULL)
		return EINVAL;
	if (!S_ISDIR(obj->mode))
		return ENOTSUP;
	if (csize == 0)
		csize = dfs->attr.da_chunk_size;

	rc = set_chunk_size(dfs, obj, csize);
	if (rc)
		return rc;

	/** if this is the root dir, we need to update the cached handle csize */
	if (daos_oid_cmp(obj->oid, dfs->root.oid) == 0)
		dfs->root.d.chunk_size = csize;

	return 0;
}

int
dfs_file_update_chunk_size(dfs_t *dfs, dfs_obj_t *obj, daos_size_t csize)
{
	int	rc;

	if (obj == NULL)
		return EINVAL;
	if (!S_ISREG(obj->mode))
		return EINVAL;
	if (csize == 0)
		csize = dfs->attr.da_chunk_size;

	rc = set_chunk_size(dfs, obj, csize);
	if (rc)
		return daos_der2errno(rc);

	/* need to update the array handle chunk size */
	rc = daos_array_update_chunk_size(obj->oh, csize);
	if (rc)
		return daos_der2errno(rc);

	return 0;
}

int
dfs_mkdir(dfs_t *dfs, dfs_obj_t *parent, const char *name, mode_t mode,
	  daos_oclass_id_t cid)
{
	dfs_obj_t		new_dir;
	daos_handle_t		th = DAOS_TX_NONE;
	struct dfs_entry	entry = {0};
	size_t			len;
	struct timespec		now;
	int			rc;

	if (dfs == NULL || !dfs->mounted)
		return EINVAL;
	if (dfs->amode != O_RDWR)
		return EPERM;
	if (parent == NULL)
		parent = &dfs->root;
	else if (!S_ISDIR(parent->mode))
		return ENOTDIR;

	rc = check_name(name, &len);
	if (rc)
		return rc;

	strncpy(new_dir.name, name, len + 1);

	rc = create_dir(dfs, parent, cid, &new_dir);
	if (rc)
		return rc;

	entry.oid = new_dir.oid;
	entry.mode = S_IFDIR | mode;

	rc = clock_gettime(CLOCK_REALTIME, &now);
	if (rc)
		return errno;
	entry.mtime = entry.ctime = now.tv_sec;
	entry.mtime_nano = entry.ctime_nano = now.tv_nsec;
	entry.chunk_size = parent->d.chunk_size;
	entry.oclass = parent->d.oclass;
	entry.uid = geteuid();
	entry.gid = getegid();

	rc = insert_entry(dfs->layout_v, parent->oh, th, name, len, DAOS_COND_DKEY_INSERT,
			  &entry);
	if (rc != 0) {
		daos_obj_close(new_dir.oh, NULL);
		return rc;
	}

	rc = daos_obj_close(new_dir.oh, NULL);
	if (rc != 0)
		return daos_der2errno(rc);

	return rc;
}

static int
remove_dir_contents(dfs_t *dfs, daos_handle_t th, struct dfs_entry entry)
{
	daos_handle_t	oh;
	daos_key_desc_t	kds[ENUM_DESC_NR];
	daos_anchor_t	anchor = {0};
	d_iov_t		iov;
	char		enum_buf[ENUM_DESC_BUF] = {0};
	d_sg_list_t	sgl;
	int		rc;

	D_ASSERT(S_ISDIR(entry.mode));

	rc = daos_obj_open(dfs->coh, entry.oid, DAOS_OO_RW, &oh, NULL);
	if (rc)
		return daos_der2errno(rc);

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
			D_GOTO(out, rc = daos_der2errno(rc));

		if (number == 0)
			continue;

		for (ptr = enum_buf, i = 0; i < number; i++) {
			struct dfs_entry child_entry = {0};
			bool exists;

			ptr += kds[i].kd_key_len;

			rc = fetch_entry(dfs->layout_v, oh, th, ptr, kds[i].kd_key_len, false,
					 &exists, &child_entry, 0, NULL, NULL, NULL);
			if (rc)
				D_GOTO(out, rc);

			if (!exists)
				continue;

			if (S_ISDIR(child_entry.mode)) {
				rc = remove_dir_contents(dfs, th, child_entry);
				if (rc)
					D_GOTO(out, rc);
			}

			rc = remove_entry(dfs, th, oh, ptr, kds[i].kd_key_len,
					  child_entry);
			if (rc)
				D_GOTO(out, rc);
		}
	}

out:
	daos_obj_close(oh, NULL);
	return rc;
}

int
dfs_remove(dfs_t *dfs, dfs_obj_t *parent, const char *name, bool force,
	   daos_obj_id_t *oid)
{
	struct dfs_entry	entry = {0};
	daos_handle_t		th = DAOS_TX_NONE;
	bool			exists;
	size_t			len;
	int			rc;

	if (dfs == NULL || !dfs->mounted)
		return EINVAL;
	if (dfs->amode != O_RDWR)
		return EPERM;
	if (parent == NULL)
		parent = &dfs->root;
	else if (!S_ISDIR(parent->mode))
		return ENOTDIR;

	rc = check_name(name, &len);
	if (rc)
		return rc;

	if (dfs->use_dtx) {
		rc = daos_tx_open(dfs->coh, &th, 0, NULL);
		if (rc) {
			D_ERROR("daos_tx_open() failed (%d)\n", rc);
			D_GOTO(out, rc = daos_der2errno(rc));
		}
	}

restart:
	/** Even with cond punch, need to fetch the entry to check the type */
	rc = fetch_entry(dfs->layout_v, parent->oh, th, name, len, false, &exists, &entry,
			 0, NULL, NULL, NULL);
	if (rc)
		D_GOTO(out, rc);

	if (!exists)
		D_GOTO(out, rc = ENOENT);

	if (S_ISDIR(entry.mode)) {
		uint32_t nr = 0;
		daos_handle_t oh;

		/** check if dir is empty */
		rc = daos_obj_open(dfs->coh, entry.oid, DAOS_OO_RW, &oh, NULL);
		if (rc) {
			D_ERROR("daos_obj_open() Failed (%d)\n", rc);
			D_GOTO(out, rc = daos_der2errno(rc));
		}

		rc = get_num_entries(oh, th, &nr, true);
		if (rc) {
			daos_obj_close(oh, NULL);
			D_GOTO(out, rc);
		}

		rc = daos_obj_close(oh, NULL);
		if (rc)
			D_GOTO(out, rc = daos_der2errno(rc));

		if (!force && nr != 0)
			D_GOTO(out, rc = ENOTEMPTY);

		if (force && nr != 0) {
			rc = remove_dir_contents(dfs, th, entry);
			if (rc)
				D_GOTO(out, rc);
		}
	}

	rc = remove_entry(dfs, th, parent->oh, name, len, entry);
	if (rc)
		D_GOTO(out, rc);

	if (dfs->use_dtx) {
		rc = daos_tx_commit(th, NULL);
		if (rc) {
			if (rc != -DER_TX_RESTART)
				D_ERROR("daos_tx_commit() failed (%d)\n", rc);
			D_GOTO(out, rc = daos_der2errno(rc));
		}
	}

	if (oid)
		oid_cp(oid, entry.oid);

out:
	rc = check_tx(th, rc);
	if (rc == ERESTART)
		goto restart;
	return rc;
}

static int
lookup_rel_path(dfs_t *dfs, dfs_obj_t *root, const char *path, int flags,
		dfs_obj_t **_obj, mode_t *mode, struct stat *stbuf,
		size_t depth)
{
	dfs_obj_t		parent;
	dfs_obj_t		*obj = NULL;
	char			*token;
	char			*rem, *sptr = NULL; /* bogus compiler warning */
	bool			exists;
	bool			is_root = true;
	int			daos_mode;
	struct dfs_entry	entry = {0};
	size_t			len;
	int			rc;
	bool			parent_fully_valid;

	/* Arbitrarily stop to avoid infinite recursion */
	if (depth >= DFS_MAX_RECURSION)
		return ELOOP;

	/* Only paths from root can be absolute */
	if (path[0] == '/' && daos_oid_cmp(root->oid, dfs->root.oid) != 0)
		return EINVAL;

	daos_mode = get_daos_obj_mode(flags);
	if (daos_mode == -1)
		return EINVAL;

	D_STRNDUP(rem, path, DFS_MAX_PATH - 1);
	if (rem == NULL)
		return ENOMEM;

	if (stbuf)
		memset(stbuf, 0, sizeof(struct stat));

	D_ALLOC_PTR(obj);
	if (obj == NULL)
		D_GOTO(out, rc = ENOMEM);

	oid_cp(&obj->oid, root->oid);
	oid_cp(&obj->parent_oid, root->parent_oid);
	obj->d.oclass = root->d.oclass;
	obj->d.chunk_size = root->d.chunk_size;
	obj->mode = root->mode;
	strncpy(obj->name, root->name, DFS_MAX_NAME + 1);

	rc = daos_obj_open(dfs->coh, obj->oid, daos_mode, &obj->oh, NULL);
	if (rc)
		D_GOTO(err_obj, rc = daos_der2errno(rc));

	parent.oh = obj->oh;
	parent.mode = obj->mode;
	oid_cp(&parent.oid, obj->oid);
	oid_cp(&parent.parent_oid, obj->parent_oid);

	/** get the obj entry in the path */
	for (token = strtok_r(rem, "/", &sptr);
	     token != NULL;
	     token = strtok_r(NULL, "/", &sptr)) {
		is_root = false;
lookup_rel_path_loop:

		/*
		 * Open the directory object one level up.
		 * Since fetch_entry does not support ".",
		 * we can't support ".." as the last entry,
		 * nor can we support "../.." because we don't
		 * have parent.parent_oid and parent.mode.
		 * For now, represent this partial state with
		 * parent_fully_valid.
		 */
		parent_fully_valid = true;
		if (strcmp(token, "..") == 0) {
			parent_fully_valid = false;

			/* Cannot go outside the container */
			if (daos_oid_cmp(parent.oid, dfs->root.oid) == 0) {
				D_DEBUG(DB_TRACE, "Failed to lookup path outside container: %s\n",
					path);
				D_GOTO(err_obj, rc = ENOENT);
			}

			rc = daos_obj_close(obj->oh, NULL);
			if (rc) {
				D_ERROR("daos_obj_close() Failed (%d)\n", rc);
				D_GOTO(err_obj, rc = daos_der2errno(rc));
			}

			rc = daos_obj_open(dfs->coh, parent.parent_oid,
					   daos_mode, &obj->oh, NULL);
			if (rc) {
				D_ERROR("daos_obj_open() Failed (%d)\n", rc);
				D_GOTO(err_obj, rc = daos_der2errno(rc));
			}

			oid_cp(&parent.oid, parent.parent_oid);
			parent.oh = obj->oh;

			/* TODO support fetch_entry(".") */
			token = strtok_r(NULL, "/", &sptr);
			if (!token || strcmp(token, "..") == 0)
				D_GOTO(err_obj, rc = ENOTSUP);
		}

		len = strlen(token);

		entry.chunk_size = 0;
		rc = fetch_entry(dfs->layout_v, parent.oh, DAOS_TX_NONE, token, len, true,
				 &exists, &entry, 0, NULL, NULL, NULL);
		if (rc)
			D_GOTO(err_obj, rc);

		rc = daos_obj_close(obj->oh, NULL);
		if (rc) {
			D_ERROR("daos_obj_close() Failed, "DF_RC"\n",
				DP_RC(rc));
			D_GOTO(err_obj, rc = daos_der2errno(rc));
		}

		if (!exists)
			D_GOTO(err_obj, rc = ENOENT);

		oid_cp(&obj->oid, entry.oid);
		oid_cp(&obj->parent_oid, parent.oid);
		strncpy(obj->name, token, len + 1);
		obj->mode = entry.mode;

		/** if entry is a file, open the array object and return */
		if (S_ISREG(entry.mode)) {
			/* if there are more entries, then file is not a dir */
			if (strtok_r(NULL, "/", &sptr) != NULL) {
				D_ERROR("%s is not a directory\n", obj->name);
				D_GOTO(err_obj, rc = ENOENT);
			}

			rc = daos_array_open_with_attr(dfs->coh, entry.oid,
						       DAOS_TX_NONE, daos_mode,
						       1, entry.chunk_size ?
						       entry.chunk_size :
						       dfs->attr.da_chunk_size,
						       &obj->oh, NULL);
			if (rc != 0) {
				D_ERROR("daos_array_open() Failed (%d)\n", rc);
				D_GOTO(err_obj, rc = daos_der2errno(rc));
			}

			if (stbuf) {
				daos_size_t size;

				rc = daos_array_get_size(obj->oh, DAOS_TX_NONE, &size, NULL);
				if (rc) {
					daos_array_close(obj->oh, NULL);
					D_GOTO(err_obj, rc = daos_der2errno(rc));
				}
				stbuf->st_size = size;
				stbuf->st_blocks = (stbuf->st_size + (1 << 9) - 1) >> 9;
			}
			break;
		}

		if (S_ISLNK(entry.mode)) {
			/*
			 * If there is a token after the sym link entry, treat
			 * the sym link as a directory and look up it's value.
			 */
			token = strtok_r(NULL, "/", &sptr);
			if (token) {
				dfs_obj_t *sym;

				if (!parent_fully_valid &&
				    strncmp(entry.value, "..", 2) == 0) {
					D_FREE(entry.value);
					D_GOTO(err_obj, rc = ENOTSUP);
				}

				rc = lookup_rel_path(dfs, &parent, entry.value,
						     flags, &sym, NULL, NULL,
						     depth + 1);
				if (rc) {
					D_DEBUG(DB_TRACE, "Failed to lookup symlink %s\n",
						entry.value);
					D_FREE(entry.value);
					D_GOTO(err_obj, rc);
				}

				obj->oh = sym->oh;
				parent.oh = sym->oh;
				parent.mode = sym->mode;
				oid_cp(&parent.oid, sym->oid);
				oid_cp(&parent.parent_oid, sym->parent_oid);

				D_FREE(sym);
				D_FREE(entry.value);
				obj->value = NULL;
				/*
				 * need to go to to the beginning of loop but we
				 * already did the strtok.
				 */
				goto lookup_rel_path_loop;
			}

			/* Conditionally dereference leaf symlinks */
			if (!(flags & O_NOFOLLOW)) {
				dfs_obj_t *sym;

				if (!parent_fully_valid &&
				    strncmp(entry.value, "..", 2) == 0) {
					D_FREE(entry.value);
					D_GOTO(err_obj, rc = ENOTSUP);
				}

				rc = lookup_rel_path(dfs, &parent, entry.value,
						     flags, &sym, mode, stbuf,
						     depth + 1);
				if (rc) {
					D_DEBUG(DB_TRACE,
						"Failed to lookup symlink %s\n",
						entry.value);
					D_FREE(entry.value);
					D_GOTO(err_obj, rc);
				}

				/* return this dereferenced obj */
				D_FREE(obj);
				obj = sym;
				D_FREE(entry.value);
				D_GOTO(out, rc);
			}

			/* Create a truncated version of the string */
			D_STRNDUP(obj->value, entry.value, entry.value_len + 1);
			if (obj->value == NULL) {
				D_FREE(entry.value);
				D_GOTO(out, rc = ENOMEM);
			}
			D_FREE(entry.value);
			if (stbuf)
				stbuf->st_size = entry.value_len;
			/** return the symlink obj if this is the last entry */
			break;
		}

		if (!S_ISDIR(entry.mode)) {
			D_ERROR("Invalid entry type in path.\n");
			D_GOTO(err_obj, rc = EINVAL);
		}

		/* open the directory object */
		rc = daos_obj_open(dfs->coh, entry.oid, daos_mode, &obj->oh,
				   NULL);
		if (rc) {
			D_ERROR("daos_obj_open() Failed, "DF_RC"\n", DP_RC(rc));
			D_GOTO(err_obj, rc = daos_der2errno(rc));
		}

		obj->d.chunk_size = entry.chunk_size;
		obj->d.oclass = entry.oclass;
		if (stbuf)
			stbuf->st_size = sizeof(entry);

		oid_cp(&parent.oid, obj->oid);
		oid_cp(&parent.parent_oid, obj->parent_oid);
		parent.oh = obj->oh;
		parent.mode = entry.mode;
	}

	if (mode)
		*mode = obj->mode;

	if (stbuf) {
		if (is_root) {
			memcpy(stbuf, &dfs->root_stbuf, sizeof(struct stat));
		} else {
			stbuf->st_nlink = 1;
			stbuf->st_mode = obj->mode;
			stbuf->st_uid = entry.uid;
			stbuf->st_gid = entry.gid;
			stbuf->st_mtim.tv_sec = entry.mtime;
			stbuf->st_mtim.tv_nsec = entry.mtime_nano;
			stbuf->st_ctim.tv_sec = entry.ctime;
			stbuf->st_ctim.tv_nsec = entry.ctime_nano;
			if (tspec_gt(stbuf->st_ctim, stbuf->st_mtim)) {
				stbuf->st_atim.tv_sec = entry.ctime;
				stbuf->st_atim.tv_nsec = entry.ctime_nano;
			} else {
				stbuf->st_atim.tv_sec = entry.mtime;
				stbuf->st_atim.tv_nsec = entry.mtime_nano;
			}
		}
	}

	obj->flags = flags;

out:
	D_FREE(rem);
	*_obj = obj;
	return rc;
err_obj:
	D_FREE(obj);
	goto out;
}

int
dfs_lookup(dfs_t *dfs, const char *path, int flags, dfs_obj_t **_obj,
	   mode_t *mode, struct stat *stbuf)
{
	if (dfs == NULL || !dfs->mounted)
		return EINVAL;
	if (_obj == NULL)
		return EINVAL;
	if (path == NULL || strnlen(path, DFS_MAX_PATH) > DFS_MAX_PATH - 1)
		return EINVAL;
	if (path[0] != '/')
		return EINVAL;

	/** if we added a prefix, check and skip over it */
	if (dfs->prefix) {
		if (strncmp(dfs->prefix, path, dfs->prefix_len) != 0)
			return EINVAL;

		path += dfs->prefix_len;
	}

	return lookup_rel_path(dfs, &dfs->root, path, flags, _obj,
			       mode, stbuf, 0);
}

int
readdir_int(dfs_t *dfs, dfs_obj_t *obj, daos_anchor_t *anchor, uint32_t *nr,
	    struct dirent *dirs, struct stat *stbufs)
{
	daos_key_desc_t	*kds;
	char		*enum_buf;
	uint32_t	number, key_nr, i;
	d_sg_list_t	sgl;
	int		rc = 0;

	if (dfs == NULL || !dfs->mounted)
		return EINVAL;
	if (obj == NULL || !S_ISDIR(obj->mode))
		return ENOTDIR;
	if (*nr == 0)
		return 0;
	if (dirs == NULL || anchor == NULL)
		return EINVAL;

	D_ALLOC_ARRAY(kds, *nr);
	if (kds == NULL)
		return ENOMEM;

	D_ALLOC_ARRAY(enum_buf, *nr * DFS_MAX_NAME);
	if (enum_buf == NULL) {
		D_FREE(kds);
		return ENOMEM;
	}

	key_nr = 0;
	number = *nr;
	while (!daos_anchor_is_eof(anchor)) {
		d_iov_t	iov;
		char	*ptr;

		memset(enum_buf, 0, (*nr) * DFS_MAX_NAME);

		sgl.sg_nr = 1;
		sgl.sg_nr_out = 0;
		d_iov_set(&iov, enum_buf, (*nr) * DFS_MAX_NAME);
		sgl.sg_iovs = &iov;

		rc = daos_obj_list_dkey(obj->oh, DAOS_TX_NONE, &number, kds,
					&sgl, anchor, NULL);
		if (rc)
			D_GOTO(out, rc = daos_der2errno(rc));

		for (ptr = enum_buf, i = 0; i < number; i++) {
			int len;

			len = snprintf(dirs[key_nr].d_name,
				       kds[i].kd_key_len + 1, "%s", ptr);
			D_ASSERT(len >= kds[i].kd_key_len);
			ptr += kds[i].kd_key_len;

			/** stat the entry if requested */
			if (stbufs) {
				rc = entry_stat(dfs, DAOS_TX_NONE, obj->oh, dirs[key_nr].d_name,
						kds[i].kd_key_len, NULL, true, &stbufs[key_nr],
						NULL);
				if (rc) {
					D_ERROR("Failed to stat entry '%s': %d (%s)\n",
						dirs[key_nr].d_name, rc, strerror(rc));
					D_GOTO(out, rc);
				}
			}
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
dfs_readdir(dfs_t *dfs, dfs_obj_t *obj, daos_anchor_t *anchor, uint32_t *nr,
	    struct dirent *dirs)
{
	return readdir_int(dfs, obj, anchor, nr, dirs, NULL);
}

int
dfs_readdirplus(dfs_t *dfs, dfs_obj_t *obj, daos_anchor_t *anchor, uint32_t *nr,
		struct dirent *dirs, struct stat *stbufs)
{
	return readdir_int(dfs, obj, anchor, nr, dirs, stbufs);
}

int
dfs_iterate(dfs_t *dfs, dfs_obj_t *obj, daos_anchor_t *anchor,
	    uint32_t *nr, size_t size, dfs_filler_cb_t op, void *udata)
{
	daos_key_desc_t	*kds;
	d_sg_list_t	sgl;
	d_iov_t		iov;
	uint32_t	num, keys_nr;
	char		*enum_buf, *ptr;
	int		rc = 0;

	if (dfs == NULL || !dfs->mounted)
		return EINVAL;
	if (obj == NULL || !S_ISDIR(obj->mode))
		return ENOTDIR;
	if (size == 0 || *nr == 0)
		return 0;
	if (anchor == NULL)
		return EINVAL;

	num = *nr;
	D_ALLOC_ARRAY(kds, num);
	if (kds == NULL)
		return ENOMEM;

	/** Allocate a buffer to store the entry keys */
	D_ALLOC_ARRAY(enum_buf, size);
	if (enum_buf == NULL) {
		D_FREE(kds);
		return ENOMEM;
	}

	sgl.sg_nr = 1;
	sgl.sg_nr_out = 0;
	d_iov_set(&iov, enum_buf, size);
	sgl.sg_iovs = &iov;
	keys_nr = 0;
	ptr = enum_buf;

	while (!daos_anchor_is_eof(anchor)) {
		uint32_t i;

		/*
		 * list num or less entries, but not more than we can fit in
		 * enum_buf
		 */
		rc = daos_obj_list_dkey(obj->oh, DAOS_TX_NONE, &num, kds,
					&sgl, anchor, NULL);
		if (rc)
			D_GOTO(out, rc = daos_der2errno(rc));

		/** for every entry, issue the filler cb */
		for (i = 0; i < num; i++) {
			if (op) {
				char term_char;

				term_char = ptr[kds[i].kd_key_len];
				ptr[kds[i].kd_key_len] = '\0';
				rc = op(dfs, obj, ptr, udata);
				if (rc)
					D_GOTO(out, rc);

				ptr[kds[i].kd_key_len] = term_char;
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

static int
dfs_lookup_rel_int(dfs_t *dfs, dfs_obj_t *parent, const char *name, int flags,
		   dfs_obj_t **_obj, mode_t *mode, struct stat *stbuf, int xnr,
		   char *xnames[], void *xvals[], daos_size_t *xsizes)
{
	dfs_obj_t		*obj;
	struct dfs_entry	entry = {0};
	bool			exists;
	int			daos_mode;
	size_t			len;
	int			rc = 0;

	if (dfs == NULL || !dfs->mounted)
		return EINVAL;
	if (_obj == NULL)
		return EINVAL;
	if (parent == NULL)
		parent = &dfs->root;
	else if (!S_ISDIR(parent->mode))
		return ENOTDIR;

	rc = check_name(name, &len);
	if (rc)
		return rc;

	daos_mode = get_daos_obj_mode(flags);
	if (daos_mode == -1)
		return EINVAL;

	rc = fetch_entry(dfs->layout_v, parent->oh, DAOS_TX_NONE, name, len, true, &exists,
			 &entry, xnr, xnames, xvals, xsizes);
	if (rc)
		return rc;

	if (!exists)
		return ENOENT;

	if (stbuf)
		memset(stbuf, 0, sizeof(struct stat));

	D_ALLOC_PTR(obj);
	if (obj == NULL) {
		D_FREE(entry.value);
		return ENOMEM;
	}

	strncpy(obj->name, name, len + 1);
	oid_cp(&obj->parent_oid, parent->oid);
	oid_cp(&obj->oid, entry.oid);
	obj->mode = entry.mode;

	/** if entry is a file, open the array object and return */
	switch (entry.mode & S_IFMT) {
	case S_IFREG:
		rc = daos_array_open_with_attr(dfs->coh, entry.oid,
					       DAOS_TX_NONE, daos_mode, 1,
					       entry.chunk_size ?
					       entry.chunk_size :
					       dfs->attr.da_chunk_size,
					       &obj->oh, NULL);
		if (rc != 0) {
			D_ERROR("daos_array_open_with_attr() Failed "DF_RC"\n", DP_RC(rc));
			D_GOTO(err_obj, rc = daos_der2errno(rc));
		}

		/** we need the file size if stat struct is needed */
		if (stbuf) {
			daos_array_stbuf_t	array_stbuf = {0};

			rc = daos_array_stat(obj->oh, DAOS_TX_NONE, &array_stbuf, NULL);
			if (rc) {
				daos_array_close(obj->oh, NULL);
				D_GOTO(err_obj, rc = daos_der2errno(rc));
			}
			stbuf->st_size = array_stbuf.st_size;
			stbuf->st_blocks = (stbuf->st_size + (1 << 9) - 1) >> 9;

			rc = update_stbuf_times(entry, array_stbuf.st_max_epoch, stbuf, NULL);
			if (rc) {
				daos_array_close(obj->oh, NULL);
				D_GOTO(err_obj, rc);
			}
		}
		break;
	case S_IFLNK:
		if (flags & O_NOFOLLOW) {
			if (entry.value == NULL) {
				D_ERROR("Symlink entry found with no value\n");
				D_GOTO(err_obj, rc = EIO);
			}
			/* Create a truncated version of the string */
			D_STRNDUP(obj->value, entry.value, entry.value_len + 1);
			D_FREE(entry.value);
			if (obj->value == NULL)
				D_GOTO(err_obj, rc = ENOMEM);
			if (stbuf) {
				stbuf->st_size = entry.value_len;
				stbuf->st_mtim.tv_sec = entry.mtime;
				stbuf->st_mtim.tv_nsec = entry.mtime_nano;
				stbuf->st_ctim.tv_sec = entry.ctime;
				stbuf->st_ctim.tv_nsec = entry.ctime_nano;
			}
		} else {
			dfs_obj_t *sym;

			/** this should not happen, but to silence coverity */
			if (entry.value == NULL)
				D_GOTO(err_obj, rc = EIO);
			/* dereference the symlink */
			rc = lookup_rel_path(dfs, parent, entry.value, flags,
					     &sym, mode, stbuf, 0);
			if (rc) {
				D_DEBUG(DB_TRACE,
					"Failed to lookup symlink %s\n",
					entry.value);
				D_FREE(entry.value);
				D_GOTO(err_obj, rc);
			}
			D_FREE(obj);
			obj = sym;
			D_FREE(entry.value);
			D_GOTO(out, rc);
		}
		break;
	case S_IFDIR:
		rc = daos_obj_open(dfs->coh, entry.oid, daos_mode, &obj->oh, NULL);
		if (rc) {
			D_ERROR("daos_obj_open() Failed: " DF_RC "\n", DP_RC(rc));
			D_GOTO(err_obj, rc = daos_der2errno(rc));
		}

		obj->d.chunk_size = entry.chunk_size;
		obj->d.oclass = entry.oclass;

		if (stbuf) {
			daos_epoch_t	ep;

			rc = daos_obj_query_max_epoch(obj->oh, DAOS_TX_NONE, &ep, NULL);
			if (rc) {
				daos_obj_close(obj->oh, NULL);
				D_GOTO(err_obj, rc = daos_der2errno(rc));
			}

			rc = update_stbuf_times(entry, ep, stbuf, NULL);
			if (rc) {
				daos_obj_close(obj->oh, NULL);
				D_GOTO(err_obj, rc = daos_der2errno(rc));
			}
			stbuf->st_size = sizeof(entry);
		}
		break;
	default:
		rc = EINVAL;
		D_ERROR("Invalid entry type (not a dir, file, symlink): %d (%s)\n", rc,
			strerror(rc));
		D_GOTO(err_obj, rc);
	}

	if (mode)
		*mode = obj->mode;

	if (stbuf) {
		stbuf->st_nlink = 1;
		stbuf->st_mode = obj->mode;
		stbuf->st_uid = entry.uid;
		stbuf->st_gid = entry.gid;
		if (tspec_gt(stbuf->st_ctim, stbuf->st_mtim)) {
			stbuf->st_atim.tv_sec = stbuf->st_ctim.tv_sec;
			stbuf->st_atim.tv_nsec = stbuf->st_ctim.tv_nsec;
		} else {
			stbuf->st_atim.tv_sec = stbuf->st_mtim.tv_sec;
			stbuf->st_atim.tv_nsec = stbuf->st_mtim.tv_nsec;
		}
	}

out:
	obj->flags = flags;
	*_obj = obj;

	return rc;
err_obj:
	D_FREE(obj);
	return rc;
}

int
dfs_lookup_rel(dfs_t *dfs, dfs_obj_t *parent, const char *name, int flags,
	       dfs_obj_t **obj, mode_t *mode, struct stat *stbuf)
{
	return dfs_lookup_rel_int(dfs, parent, name, flags, obj, mode, stbuf,
				  0, NULL, NULL, NULL);
}

int
dfs_lookupx(dfs_t *dfs, dfs_obj_t *parent, const char *name, int flags,
	    dfs_obj_t **obj, mode_t *mode, struct stat *stbuf, int xnr,
	    char *xnames[], void *xvals[], daos_size_t *xsizes)
{
	return dfs_lookup_rel_int(dfs, parent, name, flags, obj, mode, stbuf,
				  xnr, xnames, xvals, xsizes);
}

int
dfs_open(dfs_t *dfs, dfs_obj_t *parent, const char *name, mode_t mode,
	 int flags, daos_oclass_id_t cid, daos_size_t chunk_size,
	 const char *value, dfs_obj_t **_obj)
{
	return dfs_open_stat(dfs, parent, name, mode, flags, cid, chunk_size,
			     value, _obj, NULL);
}

int
dfs_open_stat(dfs_t *dfs, dfs_obj_t *parent, const char *name, mode_t mode,
	      int flags, daos_oclass_id_t cid, daos_size_t chunk_size,
	      const char *value, dfs_obj_t **_obj, struct stat *stbuf)
{
	struct dfs_entry	entry = {0};
	dfs_obj_t		*obj;
	size_t			len;
	daos_size_t		file_size = 0;
	int			rc;

	if (dfs == NULL || !dfs->mounted)
		return EINVAL;
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
	obj->mode = mode;
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
			stbuf->st_size = file_size;
			stbuf->st_nlink = 1;
			stbuf->st_mode = entry.mode;
			stbuf->st_uid = entry.uid;
			stbuf->st_gid = entry.gid;
			stbuf->st_mtim.tv_sec = entry.mtime;
			stbuf->st_mtim.tv_nsec = entry.mtime_nano;
			stbuf->st_ctim.tv_sec = entry.ctime;
			stbuf->st_ctim.tv_nsec = entry.ctime_nano;
			if (tspec_gt(stbuf->st_ctim, stbuf->st_mtim)) {
				stbuf->st_atim.tv_sec = entry.ctime;
				stbuf->st_atim.tv_nsec = entry.ctime_nano;
			} else {
				stbuf->st_atim.tv_sec = entry.mtime;
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
dfs_dup(dfs_t *dfs, dfs_obj_t *obj, int flags, dfs_obj_t **_new_obj)
{
	dfs_obj_t	*new_obj;
	unsigned int	daos_mode;
	int		rc;

	if (dfs == NULL || !dfs->mounted)
		return EINVAL;
	if (obj == NULL || _new_obj == NULL)
		return EINVAL;

	daos_mode = get_daos_obj_mode(flags);
	if (daos_mode == -1)
		return EINVAL;

	D_ALLOC_PTR(new_obj);
	if (new_obj == NULL)
		return ENOMEM;

	switch (obj->mode & S_IFMT) {
	case S_IFDIR:
		rc = daos_obj_open(dfs->coh, obj->oid, daos_mode,
				   &new_obj->oh, NULL);
		if (rc)
			D_GOTO(err, rc = daos_der2errno(rc));
		break;
	case S_IFREG:
	{
		char		buf[1024];
		d_iov_t		ghdl;

		d_iov_set(&ghdl, buf, 1024);

		rc = daos_array_local2global(obj->oh, &ghdl);
		if (rc)
			D_GOTO(err, rc = daos_der2errno(rc));

		rc = daos_array_global2local(dfs->coh, ghdl, daos_mode,
					     &new_obj->oh);
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
	new_obj->mode = obj->mode;
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
	uint32_t	magic;
	uint32_t	flags;
	mode_t		mode;
	daos_obj_id_t	oid;
	daos_obj_id_t	parent_oid;
	daos_size_t	chunk_size;
	uuid_t		cont_uuid;
	uuid_t		coh_uuid;
	char		name[DFS_MAX_NAME + 1];
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
	struct dfs_obj_glob	*obj_glob;
	uuid_t			coh_uuid;
	uuid_t			cont_uuid;
	daos_size_t		glob_buf_size;
	int			rc = 0;

	if (obj == NULL || !S_ISREG(obj->mode))
		return EINVAL;

	if (glob == NULL) {
		D_ERROR("Invalid parameter, NULL glob pointer.\n");
		return EINVAL;
	}

	if (glob->iov_buf != NULL && (glob->iov_buf_len == 0 ||
				      glob->iov_buf_len < glob->iov_len)) {
		D_ERROR("Invalid parameter of glob, iov_buf %p, iov_buf_len "
			""DF_U64", iov_len "DF_U64".\n", glob->iov_buf,
			glob->iov_buf_len, glob->iov_len);
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
		D_DEBUG(DB_ANY, "Larger glob buffer needed ("DF_U64" bytes "
			"provided, "DF_U64" required).\n", glob->iov_buf_len,
			glob_buf_size);
		glob->iov_buf_len = glob_buf_size;
		return ENOBUFS;
	}
	glob->iov_len = glob_buf_size;

	/* init global handle */
	obj_glob = (struct dfs_obj_glob *)glob->iov_buf;
	obj_glob->magic		= DFS_OBJ_GLOB_MAGIC;
	obj_glob->mode		= obj->mode;
	obj_glob->flags		= obj->flags;
	oid_cp(&obj_glob->oid, obj->oid);
	oid_cp(&obj_glob->parent_oid, obj->parent_oid);
	uuid_copy(obj_glob->coh_uuid, coh_uuid);
	uuid_copy(obj_glob->cont_uuid, cont_uuid);
	strncpy(obj_glob->name, obj->name, DFS_MAX_NAME + 1);
	obj_glob->name[DFS_MAX_NAME] = 0;
	rc = dfs_get_chunk_size(obj, &obj_glob->chunk_size);
	if (rc)
		return rc;

	return rc;
}

int
dfs_obj_global2local(dfs_t *dfs, int flags, d_iov_t glob, dfs_obj_t **_obj)
{
	struct dfs_obj_glob	*obj_glob;
	dfs_obj_t		*obj;
	uuid_t			coh_uuid;
	uuid_t			cont_uuid;
	int			daos_mode;
	int			rc = 0;

	if (dfs == NULL || !dfs->mounted || _obj == NULL)
		return EINVAL;

	if (glob.iov_buf == NULL || glob.iov_buf_len < glob.iov_len ||
	    glob.iov_len != dfs_obj_glob_buf_size()) {
		D_ERROR("Invalid parameter of glob, iov_buf %p, "
			"iov_buf_len "DF_U64", iov_len "DF_U64".\n",
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
		D_ERROR("Container uuid mismatch, in coh: "DF_UUID", "
			"in obj_glob:" DF_UUID"\n", DP_UUID(cont_uuid),
			DP_UUID(obj_glob->cont_uuid));
		return EINVAL;
	}

	D_ALLOC_PTR(obj);
	if (obj == NULL)
		return ENOMEM;

	oid_cp(&obj->oid, obj_glob->oid);
	oid_cp(&obj->parent_oid, obj_glob->parent_oid);
	strncpy(obj->name, obj_glob->name, DFS_MAX_NAME + 1);
	obj->name[DFS_MAX_NAME] = '\0';
	obj->mode = obj_glob->mode;
	obj->flags = flags ? flags : obj_glob->flags;

	daos_mode = get_daos_obj_mode(obj->flags);
	rc = daos_array_open_with_attr(dfs->coh, obj->oid, DAOS_TX_NONE,
				       daos_mode, 1, obj_glob->chunk_size,
				       &obj->oh, NULL);
	if (rc) {
		D_ERROR("daos_array_open_with_attr() failed, "DF_RC"\n",
			DP_RC(rc));
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
		D_ERROR("Failed to close DFS object, "DF_RC"\n", DP_RC(rc));
	else
		D_FREE(obj);
	return daos_der2errno(rc);
}

struct dfs_read_params {
	daos_size_t		*read_size;
	daos_array_iod_t	arr_iod;
	daos_range_t		rg;
};

static int
read_cb(tse_task_t *task, void *data)
{
	struct dfs_read_params	*params;
	int			rc = task->dt_result;

	params = daos_task_get_priv(task);
	D_ASSERT(params != NULL);

	if (rc != 0) {
		D_ERROR("Failed to read from array object: "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	*params->read_size = params->arr_iod.arr_nr_read;
out:
	D_FREE(params);
	return rc;
}

static int
dfs_read_int(dfs_t *dfs, dfs_obj_t *obj, daos_off_t off, dfs_iod_t *iod,
	     d_sg_list_t *sgl, daos_size_t buf_size, daos_size_t *read_size,
	     daos_event_t *ev)
{
	tse_task_t		*task = NULL;
	daos_array_io_t		*args;
	struct dfs_read_params	*params;
	int			rc;

	D_ASSERT(ev);
	daos_event_errno_rc(ev);

	rc = dc_task_create(dc_array_read, NULL, ev, &task);
	if (rc != 0)
		return daos_der2errno(rc);

	D_ALLOC_PTR(params);
	if (params == NULL)
		D_GOTO(err_task, rc = ENOMEM);

	params->read_size = read_size;

	/** set array location */
	if (iod == NULL) {
		params->arr_iod.arr_nr	= 1;
		params->rg.rg_len	= buf_size;
		params->rg.rg_idx	= off;
		params->arr_iod.arr_rgs	= &params->rg;
	} else {
		params->arr_iod.arr_nr	= iod->iod_nr;
		params->arr_iod.arr_rgs	= iod->iod_rgs;
	}

	args		= dc_task_get_args(task);
	args->oh	= obj->oh;
	args->th	= DAOS_TX_NONE;
	args->sgl	= sgl;
	args->iod	= &params->arr_iod;

	daos_task_set_priv(task, params);
	rc = tse_task_register_cbs(task, NULL, NULL, 0, read_cb, NULL, 0);
	if (rc)
		D_GOTO(err_params, rc = daos_der2errno(rc));

	rc = dc_task_schedule(task, true);
	return daos_der2errno(rc);

err_params:
	D_FREE(params);
err_task:
	tse_task_complete(task, rc);
	/** the event is completed with the proper rc */
	return 0;
}

int
dfs_read(dfs_t *dfs, dfs_obj_t *obj, d_sg_list_t *sgl, daos_off_t off,
	 daos_size_t *read_size, daos_event_t *ev)
{
	daos_size_t		buf_size;
	int			i, rc;

	if (dfs == NULL || !dfs->mounted)
		return EINVAL;
	if (obj == NULL || !S_ISREG(obj->mode))
		return EINVAL;
	if (read_size == NULL)
		return EINVAL;
	if ((obj->flags & O_ACCMODE) == O_WRONLY)
		return EPERM;

	buf_size = 0;
	for (i = 0; i < sgl->sg_nr; i++)
		buf_size += sgl->sg_iovs[i].iov_len;
	if (buf_size == 0) {
		*read_size = 0;
		if (ev) {
			daos_event_launch(ev);
			daos_event_complete(ev, 0);
		}
		return 0;
	}

	D_DEBUG(DB_TRACE, "DFS Read: Off %"PRIu64", Len %zu\n", off, buf_size);

	if (ev == NULL) {
		daos_array_iod_t	iod;
		daos_range_t		rg;

		/** set array location */
		iod.arr_nr = 1;
		rg.rg_len = buf_size;
		rg.rg_idx = off;
		iod.arr_rgs = &rg;

		rc = daos_array_read(obj->oh, DAOS_TX_NONE, &iod, sgl, NULL);
		if (rc) {
			D_ERROR("daos_array_read() failed, "DF_RC"\n",
				DP_RC(rc));
			return daos_der2errno(rc);
		}

		*read_size = iod.arr_nr_read;
		return 0;
	}

	return dfs_read_int(dfs, obj, off, NULL, sgl, buf_size, read_size, ev);
}

int
dfs_readx(dfs_t *dfs, dfs_obj_t *obj, dfs_iod_t *iod, d_sg_list_t *sgl,
	  daos_size_t *read_size, daos_event_t *ev)
{
	int			rc;

	if (dfs == NULL || !dfs->mounted)
		return EINVAL;
	if (obj == NULL || !S_ISREG(obj->mode))
		return EINVAL;
	if (read_size == NULL)
		return EINVAL;
	if ((obj->flags & O_ACCMODE) == O_WRONLY)
		return EPERM;

	if (iod->iod_nr == 0) {
		if (ev) {
			daos_event_launch(ev);
			daos_event_complete(ev, 0);
		}
		return 0;
	}

	if (ev == NULL) {
		daos_array_iod_t	arr_iod;

		/** set array location */
		arr_iod.arr_nr = iod->iod_nr;
		arr_iod.arr_rgs = iod->iod_rgs;

		rc = daos_array_read(obj->oh, DAOS_TX_NONE, &arr_iod, sgl, ev);
		if (rc) {
			D_ERROR("daos_array_read() failed (%d)\n", rc);
			return daos_der2errno(rc);
		}

		*read_size = arr_iod.arr_nr_read;
		return 0;
	}

	return dfs_read_int(dfs, obj, 0, iod, sgl, 0, read_size, ev);
}

int
dfs_write(dfs_t *dfs, dfs_obj_t *obj, d_sg_list_t *sgl, daos_off_t off,
	  daos_event_t *ev)
{
	daos_array_iod_t	iod;
	daos_range_t		rg;
	daos_size_t		buf_size;
	int			i;
	int			rc;

	if (dfs == NULL || !dfs->mounted)
		return EINVAL;
	if (dfs->amode != O_RDWR)
		return EPERM;
	if (obj == NULL || !S_ISREG(obj->mode))
		return EINVAL;
	if ((obj->flags & O_ACCMODE) == O_RDONLY)
		return EPERM;

	buf_size = 0;
	if (sgl)
		for (i = 0; i < sgl->sg_nr; i++)
			buf_size += sgl->sg_iovs[i].iov_len;
	if (buf_size == 0) {
		if (ev) {
			daos_event_launch(ev);
			daos_event_complete(ev, 0);
		}
		return 0;
	}

	/** set array location */
	iod.arr_nr = 1;
	rg.rg_len = buf_size;
	rg.rg_idx = off;
	iod.arr_rgs = &rg;

	D_DEBUG(DB_TRACE, "DFS Write: Off %"PRIu64", Len %zu\n", off, buf_size);

	if (ev)
		daos_event_errno_rc(ev);

	rc = daos_array_write(obj->oh, DAOS_TX_NONE, &iod, sgl, ev);
	if (rc)
		D_ERROR("daos_array_write() failed, "DF_RC"\n", DP_RC(rc));

	return daos_der2errno(rc);
}

int
dfs_writex(dfs_t *dfs, dfs_obj_t *obj, dfs_iod_t *iod, d_sg_list_t *sgl,
	   daos_event_t *ev)
{
	daos_array_iod_t	arr_iod;
	int			rc;

	if (dfs == NULL || !dfs->mounted)
		return EINVAL;
	if (dfs->amode != O_RDWR)
		return EPERM;
	if (obj == NULL || !S_ISREG(obj->mode))
		return EINVAL;
	if ((obj->flags & O_ACCMODE) == O_RDONLY)
		return EPERM;
	if (iod == NULL)
		return EINVAL;

	if (iod->iod_nr == 0) {
		if (ev) {
			daos_event_launch(ev);
			daos_event_complete(ev, 0);
		}
		return 0;
	}

	/** set array location */
	arr_iod.arr_nr = iod->iod_nr;
	arr_iod.arr_rgs = iod->iod_rgs;

	if (ev)
		daos_event_errno_rc(ev);

	rc = daos_array_write(obj->oh, DAOS_TX_NONE, &arr_iod, sgl, ev);
	if (rc)
		D_ERROR("daos_array_write() failed (%d)\n", rc);

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
	daos_handle_t	oh;
	size_t		len;
	int		rc;

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
		len = strlen(parent->name);
		oh = dfs->super_oh;
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
	daos_handle_t	oh;
	int		rc;

	if (dfs == NULL || !dfs->mounted)
		return EINVAL;
	if (obj == NULL)
		return EINVAL;

	/** Open parent object and fetch entry of obj from it */
	rc = daos_obj_open(dfs->coh, obj->parent_oid, DAOS_OO_RO, &oh, NULL);
	if (rc)
		return daos_der2errno(rc);

	rc = entry_stat(dfs, DAOS_TX_NONE, oh, obj->name, strlen(obj->name), obj, true, stbuf,
			NULL);
	if (rc)
		D_GOTO(out, rc);

out:
	daos_obj_close(oh, NULL);
	return rc;
}

struct dfs_statx_args {
	dfs_t			*dfs;
	dfs_obj_t		*obj;
	struct stat		*stbuf;
	daos_handle_t		parent_oh;
};

struct statx_op_args {
	daos_key_t		dkey;
	daos_iod_t		iod;
	daos_recx_t		recx;
	d_sg_list_t		sgl;
	d_iov_t			sg_iovs[INODE_AKEYS];
	struct dfs_entry	entry;
	daos_array_stbuf_t	array_stbuf;
};

int
ostatx_cb(tse_task_t *task, void *data)
{
	struct dfs_statx_args	*args = daos_task_get_args(task);
	struct statx_op_args	*op_args = *((struct statx_op_args **)data);
	int			rc2, rc = task->dt_result;

	if (rc != 0) {
		D_CDEBUG(rc == -DER_NONEXIST, DLOG_DBG, DLOG_ERR,
			 "Failed to stat file: " DF_RC "\n", DP_RC(rc));
		/** convert the task result to errno since it takes prescedence over the cb error */
		task->dt_result = daos_der2errno(rc);
		D_GOTO(out, rc = task->dt_result);
	}

	if (args->obj->oid.hi != op_args->entry.oid.hi ||
	    args->obj->oid.lo != op_args->entry.oid.lo)
		D_GOTO(out, rc = ENOENT);

	rc = update_stbuf_times(op_args->entry, op_args->array_stbuf.st_max_epoch, args->stbuf, NULL);
	if (rc)
		D_GOTO(out, rc);

	if (S_ISREG(args->obj->mode)) {
		args->stbuf->st_size = op_args->array_stbuf.st_size;
		args->stbuf->st_blocks = (args->stbuf->st_size + (1 << 9) - 1) >> 9;
	} else if (S_ISDIR(args->obj->mode)) {
		args->stbuf->st_size = sizeof(op_args->entry);
	} else if (S_ISLNK(args->obj->mode)) {
		args->stbuf->st_size = op_args->entry.value_len;
	}

	args->stbuf->st_nlink = 1;
	args->stbuf->st_mode = op_args->entry.mode;
	args->stbuf->st_uid = op_args->entry.uid;
	args->stbuf->st_gid = op_args->entry.gid;
	if (tspec_gt(args->stbuf->st_ctim, args->stbuf->st_mtim)) {
		args->stbuf->st_atim.tv_sec = args->stbuf->st_ctim.tv_sec;
		args->stbuf->st_atim.tv_nsec = args->stbuf->st_ctim.tv_nsec;
	} else {
		args->stbuf->st_atim.tv_sec = args->stbuf->st_mtim.tv_sec;
		args->stbuf->st_atim.tv_nsec = args->stbuf->st_mtim.tv_nsec;
	}

out:
	D_FREE(op_args);
	rc2 = daos_obj_close(args->parent_oh, NULL);
	if (rc == 0)
		rc = daos_der2errno(rc2);
	return rc;
}

int
statx_task(tse_task_t *task)
{
	struct dfs_statx_args	*args = daos_task_get_args(task);
	struct statx_op_args	*op_args;
	tse_task_t		*fetch_task;
	daos_obj_fetch_t	*fetch_arg;
	tse_task_t		*stat_task;
	tse_sched_t		*sched = tse_task2sched(task);
	bool			need_stat = false;
	int			i;
	int			rc;

	D_ALLOC_PTR(op_args);
	if (op_args == NULL) {
		daos_obj_close(args->parent_oh, NULL);
		return ENOMEM;
	}

	/** Create task to fetch entry. */
	rc = daos_task_create(DAOS_OPC_OBJ_FETCH, sched, 0, NULL, &fetch_task);
	if (rc != 0) {
		D_ERROR("daos_task_create() failed: "DF_RC"\n", DP_RC(rc));
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
	i = 0;
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
	op_args->sgl.sg_nr		= i;
	op_args->sgl.sg_nr_out	= 0;
	op_args->sgl.sg_iovs	= op_args->sg_iovs;

	fetch_arg = daos_task_get_args(fetch_task);
	fetch_arg->oh		= args->parent_oh;
	fetch_arg->th		= DAOS_TX_NONE;
	fetch_arg->flags	= DAOS_COND_DKEY_FETCH;
	fetch_arg->dkey		= &op_args->dkey;
	fetch_arg->nr		= 1;
	fetch_arg->iods		= &op_args->iod;
	fetch_arg->sgls		= &op_args->sgl;

	if (S_ISREG(args->obj->mode)) {
		daos_array_stat_t *stat_arg;

		rc = daos_task_create(DAOS_OPC_ARRAY_STAT, sched, 0, NULL, &stat_task);
		if (rc != 0) {
			D_ERROR("daos_task_create() failed: "DF_RC"\n", DP_RC(rc));
			D_GOTO(err2_out, rc);
		}

		/** set array_stat parameters */
		stat_arg = daos_task_get_args(stat_task);
		stat_arg->oh	= args->obj->oh;
		stat_arg->th	= DAOS_TX_NONE;
		stat_arg->stbuf	= &op_args->array_stbuf;
		need_stat = true;
	} else if (S_ISDIR(args->obj->mode)) {
		daos_obj_query_key_t *stat_arg;

		rc = daos_task_create(DAOS_OPC_OBJ_QUERY_KEY, sched, 0, NULL, &stat_task);
		if (rc != 0) {
			D_ERROR("daos_task_create() failed: "DF_RC"\n", DP_RC(rc));
			D_GOTO(err2_out, rc);
		}

		/** set obj_query parameters */
		stat_arg = daos_task_get_args(stat_task);
		stat_arg->oh		= args->obj->oh;
		stat_arg->th		= DAOS_TX_NONE;
		stat_arg->max_epoch	= &op_args->array_stbuf.st_max_epoch;
		stat_arg->flags		= 0;
		stat_arg->dkey		= NULL;
		stat_arg->akey		= NULL;
		stat_arg->recx		= NULL;
		need_stat = true;
	}

	rc = tse_task_register_deps(task, 1, &fetch_task);
	if (rc) {
		D_ERROR("tse_task_register_deps() failed: "DF_RC"\n", DP_RC(rc));
		D_GOTO(err3_out, rc);
	}
	if (need_stat) {
		rc = tse_task_register_deps(task, 1, &stat_task);
		if (rc) {
			D_ERROR("tse_task_register_deps() failed: "DF_RC"\n", DP_RC(rc));
			tse_task_complete(stat_task, rc);
			D_GOTO(err1_out, rc);
		}
	}
	rc = tse_task_register_comp_cb(task, ostatx_cb, &op_args, sizeof(op_args));
	if (rc != 0) {
		D_ERROR("tse_task_register_comp_cb() failed: "DF_RC"\n", DP_RC(rc));
		D_GOTO(err1_out, rc);
	}

	tse_task_schedule(fetch_task, true);
	if (need_stat)
		tse_task_schedule(stat_task, true);
	return daos_der2errno(rc);
err3_out:
	if (need_stat)
		tse_task_complete(stat_task, rc);
err2_out:
	tse_task_complete(fetch_task, rc);
err1_out:
	D_FREE(op_args);
	daos_obj_close(args->parent_oh, NULL);
	return daos_der2errno(rc);
}

int
dfs_ostatx(dfs_t *dfs, dfs_obj_t *obj, struct stat *stbuf, daos_event_t *ev)
{
	daos_handle_t		oh;
	tse_task_t		*task;
	struct dfs_statx_args	*args;
	int			rc;

	if (dfs == NULL || !dfs->mounted)
		return EINVAL;
	if (obj == NULL)
		return EINVAL;

	rc = daos_obj_open(dfs->coh, obj->parent_oid, DAOS_OO_RO, &oh, NULL);
	if (rc)
		return daos_der2errno(rc);

	rc = dc_task_create(statx_task, NULL, ev, &task);
	if (rc) {
		daos_obj_close(oh, NULL);
		return rc;
	}

	args = dc_task_get_args(task);
	args->dfs	= dfs;
	args->obj	= obj;
	args->parent_oh	= oh;
	args->stbuf	= stbuf;

	rc = dc_task_schedule(task, true);
	if (rc) {
		daos_obj_close(oh, NULL);
		return rc;
	}
	return 0;
}

int
dfs_access(dfs_t *dfs, dfs_obj_t *parent, const char *name, int mask)
{
	daos_handle_t		oh;
	bool			exists;
	struct dfs_entry	entry = {0};
	size_t			len;
	dfs_obj_t		*sym;
	int			rc;

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
			D_ERROR("Invalid path %s and entry name is NULL\n",
				parent->name);
			return EINVAL;
		}
		name = parent->name;
		len = strlen(name);
		oh = dfs->super_oh;
	} else {
		rc = check_name(name, &len);
		if (rc)
			return rc;
		oh = parent->oh;
	}

	/* Check if parent has the entry */
	rc = fetch_entry(dfs->layout_v, oh, DAOS_TX_NONE, name, len, true, &exists, &entry,
			 0, NULL, NULL, NULL);
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
	daos_handle_t		oh;
	daos_handle_t		th = DAOS_TX_NONE;
	bool			exists;
	struct dfs_entry	entry = {0};
	d_sg_list_t		sgl;
	d_iov_t			sg_iovs[3];
	daos_iod_t		iod;
	daos_recx_t		recxs[3];
	daos_key_t		dkey;
	size_t			len;
	dfs_obj_t		*sym;
	mode_t			orig_mode;
	const char		*entry_name;
	struct timespec		now;
	int			rc;

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
			D_ERROR("Invalid path %s and entry name is NULL)\n",
				parent->name);
			return EINVAL;
		}
		name = parent->name;
		len = strlen(name);
		oh = dfs->super_oh;
	} else {
		rc = check_name(name, &len);
		if (rc)
			return rc;
		oh = parent->oh;
	}

	/** sticky bit, set-user-id and set-group-id, are not supported */
	if (mode & S_ISVTX || mode & S_ISGID || mode & S_ISUID) {
		D_ERROR("setuid, setgid, & sticky bit are not supported.\n");
		return ENOTSUP;
	}

	/* Check if parent has the entry */
	rc = fetch_entry(dfs->layout_v, oh, DAOS_TX_NONE, name, len, true, &exists, &entry,
			 0, NULL, NULL, NULL);
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

		orig_mode = sym->mode;
		entry_name = sym->name;
		len = strlen(entry_name);
	} else {
		orig_mode = entry.mode;
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
	iod.iod_recxs	= recxs;
	iod.iod_type	= DAOS_IOD_ARRAY;
	iod.iod_size	= 1;
	iod.iod_nr	= 3;
	recxs[0].rx_idx	= MODE_IDX;
	recxs[0].rx_nr	= sizeof(mode_t);
	recxs[1].rx_idx	= CTIME_IDX;
	recxs[1].rx_nr	= sizeof(uint64_t);
	recxs[2].rx_idx	= CTIME_NSEC_IDX;
	recxs[2].rx_nr	= sizeof(uint64_t);

	rc = clock_gettime(CLOCK_REALTIME, &now);
	if (rc)
		D_GOTO(out, rc = errno);

	/** set sgl for update */
	sgl.sg_nr	= 3;
	sgl.sg_nr_out	= 0;
	sgl.sg_iovs	= &sg_iovs[0];
	d_iov_set(&sg_iovs[0], &mode, sizeof(mode_t));
	d_iov_set(&sg_iovs[1], &now.tv_sec, sizeof(uint64_t));
	d_iov_set(&sg_iovs[2], &now.tv_nsec, sizeof(uint64_t));

	rc = daos_obj_update(oh, th, DAOS_COND_DKEY_UPDATE, &dkey, 1, &iod, &sgl, NULL);
	if (rc) {
		D_ERROR("Failed to update mode, "DF_RC"\n", DP_RC(rc));
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
	daos_handle_t		oh;
	daos_handle_t		th = DAOS_TX_NONE;
	bool			exists;
	struct dfs_entry	entry = {0};
	daos_key_t		dkey;
	d_sg_list_t		sgl;
	d_iov_t			sg_iovs[4];
	daos_iod_t		iod;
	daos_recx_t		recxs[4];
	size_t			len;
	dfs_obj_t		*sym;
	const char		*entry_name;
	int			i;
	struct timespec		now;
	int			rc;

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
		len = strlen(name);
		oh = dfs->super_oh;
	} else {
		rc = check_name(name, &len);
		if (rc)
			return rc;
		oh = parent->oh;
	}

	/* Check if parent has the entry */
	rc = fetch_entry(dfs->layout_v, oh, DAOS_TX_NONE, name, len, true, &exists, &entry,
			 0, NULL, NULL, NULL);
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
			D_DEBUG(DB_TRACE, "Failed to lookup symlink '%s': %d (%s)\n",
				entry.value, rc, strerror(rc));
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
		len = strlen(entry_name);
	} else {
		if (S_ISLNK(entry.mode))
			D_FREE(entry.value);
		entry_name = name;
	}

	rc = clock_gettime(CLOCK_REALTIME, &now);
	if (rc)
		D_GOTO(out, rc = errno);

	i = 0;
	recxs[i].rx_idx	= CTIME_IDX;
	recxs[i].rx_nr	= sizeof(uint64_t);
	d_iov_set(&sg_iovs[i], &now.tv_sec, sizeof(uint64_t));
	i++;

	recxs[i].rx_idx	= CTIME_NSEC_IDX;
	recxs[i].rx_nr	= sizeof(uint64_t);
	d_iov_set(&sg_iovs[i], &now.tv_nsec, sizeof(uint64_t));
	i++;

	/** not enforcing any restriction on chown more than write access to the container */
	if (uid != -1) {
		d_iov_set(&sg_iovs[i], &uid, sizeof(uid_t));
		recxs[i].rx_idx	= UID_IDX;
		recxs[i].rx_nr	= sizeof(uid_t);
		i++;
	}
	if (gid != -1) {
		d_iov_set(&sg_iovs[i], &gid, sizeof(gid_t));
		recxs[i].rx_idx	= GID_IDX;
		recxs[i].rx_nr	= sizeof(gid_t);
		i++;
	}

	/** set dkey as the entry name */
	d_iov_set(&dkey, (void *)entry_name, len);
	d_iov_set(&iod.iod_name, INODE_AKEY_NAME, sizeof(INODE_AKEY_NAME) - 1);
	iod.iod_nr	= i;
	iod.iod_recxs	= recxs;
	iod.iod_type	= DAOS_IOD_ARRAY;
	iod.iod_size	= 1;

	/** set sgl for update */
	sgl.sg_nr	= i;
	sgl.sg_nr_out	= 0;
	sgl.sg_iovs	= &sg_iovs[0];

	rc = daos_obj_update(oh, th, DAOS_COND_DKEY_UPDATE, &dkey, 1, &iod, &sgl, NULL);
	if (rc) {
		D_ERROR("Failed to update owner/group, "DF_RC"\n", DP_RC(rc));
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
	daos_handle_t		th = DAOS_TX_NONE;
	daos_key_t		dkey;
	daos_handle_t		oh;
	d_sg_list_t		sgl;
	d_iov_t			sg_iovs[10];
	daos_iod_t		iod;
	daos_recx_t		recxs[10];
	bool			set_size = false;
	bool			set_mtime = false;
	bool			set_ctime = false;
	int			i = 0;
	size_t			len;
	int			rc;
	uint64_t		obj_hlc = 0;
	struct stat		rstat = {};

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
		/** sticky bit, set-user-id and set-group-id not supported */
		if (stbuf->st_mode & S_ISVTX || stbuf->st_mode & S_ISGID ||
		    stbuf->st_mode & S_ISUID) {
			D_DEBUG(DB_TRACE, "setuid, setgid, & sticky bit are not"
				" supported.\n");
			return EINVAL;
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
	iod.iod_recxs	= recxs;
	iod.iod_type	= DAOS_IOD_ARRAY;
	iod.iod_size	= 1;

	/** need to update ctime */
	if (flags & DFS_SET_ATTR_MODE || flags & DFS_SET_ATTR_MTIME ||
	    flags & DFS_SET_ATTR_UID || flags & DFS_SET_ATTR_GID) {
		struct timespec	now;

		rc = clock_gettime(CLOCK_REALTIME, &now);
		if (rc)
			D_GOTO(out_obj, rc = errno);
		rstat.st_ctim.tv_sec = now.tv_sec;
		rstat.st_ctim.tv_nsec = now.tv_nsec;
		set_ctime = true;

		d_iov_set(&sg_iovs[i], &rstat.st_ctim.tv_sec, sizeof(uint64_t));
		recxs[i].rx_idx = CTIME_IDX;
		recxs[i].rx_nr = sizeof(uint64_t);
		i++;

		d_iov_set(&sg_iovs[i], &rstat.st_ctim.tv_nsec, sizeof(uint64_t));
		recxs[i].rx_idx = CTIME_NSEC_IDX;
		recxs[i].rx_nr = sizeof(uint64_t);
		i++;
	}

	if (flags & DFS_SET_ATTR_MODE) {
		d_iov_set(&sg_iovs[i], &stbuf->st_mode, sizeof(mode_t));
		recxs[i].rx_idx = MODE_IDX;
		recxs[i].rx_nr = sizeof(mode_t);
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
		recxs[i].rx_nr = sizeof(uint64_t);
		i++;

		d_iov_set(&sg_iovs[i], &stbuf->st_mtim.tv_nsec, sizeof(uint64_t));
		recxs[i].rx_idx = MTIME_NSEC_IDX;
		recxs[i].rx_nr = sizeof(uint64_t);
		i++;

		d_iov_set(&sg_iovs[i], &obj_hlc, sizeof(uint64_t));
		recxs[i].rx_idx = HLC_IDX;
		recxs[i].rx_nr = sizeof(uint64_t);
		i++;

		set_mtime = true;
		flags &= ~DFS_SET_ATTR_MTIME;
		rstat.st_mtim.tv_sec = stbuf->st_mtim.tv_sec;
		rstat.st_mtim.tv_nsec = stbuf->st_mtim.tv_nsec;
	}
	if (flags & DFS_SET_ATTR_UID) {
		d_iov_set(&sg_iovs[i], &stbuf->st_uid, sizeof(uid_t));
		recxs[i].rx_idx = UID_IDX;
		recxs[i].rx_nr = sizeof(uid_t);
		i++;
		flags &= ~DFS_SET_ATTR_UID;
		rstat.st_uid = stbuf->st_uid;
	}
	if (flags & DFS_SET_ATTR_GID) {
		d_iov_set(&sg_iovs[i], &stbuf->st_gid, sizeof(gid_t));
		recxs[i].rx_idx = GID_IDX;
		recxs[i].rx_nr = sizeof(gid_t);
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
		rstat.st_size = stbuf->st_size;

		/* mtime and ctime need to be updated too only if not set earlier */
		if (!set_mtime || !set_ctime) {
			daos_array_stbuf_t	array_stbuf = {0};

			/** TODO - need an array API to just stat the max epoch without size */
			rc = daos_array_stat(obj->oh, th, &array_stbuf, NULL);
			if (rc)
				D_GOTO(out_obj, rc = daos_der2errno(rc));

			if (!set_mtime) {
				rc = d_hlc2timespec(array_stbuf.st_max_epoch, &rstat.st_mtim);
				if (rc) {
					D_ERROR("d_hlc2timespec() failed "DF_RC"\n", DP_RC(rc));
					D_GOTO(out_obj, rc = daos_der2errno(rc));
				}
			}

			if (!set_ctime) {
				rc = d_hlc2timespec(array_stbuf.st_max_epoch, &rstat.st_ctim);
				if (rc) {
					D_ERROR("d_hlc2timespec() failed "DF_RC"\n", DP_RC(rc));
					D_GOTO(out_obj, rc = daos_der2errno(rc));
				}
			}
		}
	}

	iod.iod_nr = i;

	if (i == 0)
		D_GOTO(out_stat, rc = 0);

	sgl.sg_nr	= i;
	sgl.sg_nr_out	= 0;
	sgl.sg_iovs	= &sg_iovs[0];

	rc = daos_obj_update(oh, th, DAOS_COND_DKEY_UPDATE, &dkey, 1, &iod, &sgl, NULL);
	if (rc) {
		D_ERROR("Failed to update attr "DF_RC"\n", DP_RC(rc));
		D_GOTO(out_obj, rc = daos_der2errno(rc));
	}

out_stat:
	*stbuf = rstat;
out_obj:
	daos_obj_close(oh, NULL);
	return rc;
}

int
dfs_get_size(dfs_t *dfs, dfs_obj_t *obj, daos_size_t *size)
{
	int rc;

	if (dfs == NULL || !dfs->mounted)
		return EINVAL;
	if (obj == NULL || !S_ISREG(obj->mode))
		return EINVAL;

	rc = daos_array_get_size(obj->oh, DAOS_TX_NONE, size, NULL);
	return daos_der2errno(rc);
}

int
dfs_punch(dfs_t *dfs, dfs_obj_t *obj, daos_off_t offset, daos_size_t len)
{
	daos_size_t		size;
	daos_array_iod_t	iod;
	daos_range_t		rg;
	daos_off_t		hi;
	int			rc;

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
	iod.arr_nr = 1;
	rg.rg_len = len;
	rg.rg_idx = offset;
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

static int
xattr_copy(daos_handle_t src_oh, const char *src_name, daos_handle_t dst_oh, const char *dst_name,
	   daos_handle_t th)
{
	daos_key_t	src_dkey, dst_dkey;
	daos_anchor_t	anchor = {0};
	d_sg_list_t	sgl, fsgl;
	d_iov_t		iov, fiov;
	daos_iod_t	iod;
	void		*val_buf;
	char		enum_buf[ENUM_XDESC_BUF];
	daos_key_desc_t	kds[ENUM_DESC_NR];
	int		rc = 0;

	/** set dkey for src entry name */
	d_iov_set(&src_dkey, (void *)src_name, strlen(src_name));

	/** set dkey for dst entry name */
	d_iov_set(&dst_dkey, (void *)dst_name, strlen(dst_name));

	/** Set IOD descriptor for fetching every xattr */
	iod.iod_nr	= 1;
	iod.iod_recxs	= NULL;
	iod.iod_type	= DAOS_IOD_SINGLE;
	iod.iod_size	= DFS_MAX_XATTR_LEN;

	/** set sgl for fetch - user a preallocated buf to avoid a roundtrip */
	D_ALLOC(val_buf, DFS_MAX_XATTR_LEN);
	if (val_buf == NULL)
		return ENOMEM;
	fsgl.sg_nr	= 1;
	fsgl.sg_nr_out	= 0;
	fsgl.sg_iovs	= &fiov;

	/** set sgl for akey_list */
	sgl.sg_nr = 1;
	sgl.sg_nr_out = 0;
	d_iov_set(&iov, enum_buf, ENUM_XDESC_BUF);
	sgl.sg_iovs = &iov;

	/** iterate over every akey to look for xattrs */
	while (!daos_anchor_is_eof(&anchor)) {
		uint32_t	number = ENUM_DESC_NR;
		uint32_t	i;
		char		*ptr;

		memset(enum_buf, 0, ENUM_XDESC_BUF);
		rc = daos_obj_list_akey(src_oh, th, &src_dkey, &number, kds,
					&sgl, &anchor, NULL);
		if (rc == -DER_TX_RESTART) {
			D_DEBUG(DB_TRACE, "daos_obj_list_akey() failed (%d)\n", rc);
			D_GOTO(out, rc = daos_der2errno(rc));
		} else if (rc) {
			D_ERROR("daos_obj_list_akey() failed (%d)\n", rc);
			D_GOTO(out, rc = daos_der2errno(rc));
		}

		/** nothing enumerated, continue loop */
		if (number == 0)
			continue;

		/*
		 * for every entry enumerated, check if it's an xattr, and
		 * insert it in the new entry.
		 */
		for (ptr = enum_buf, i = 0; i < number; i++) {
			/** if not an xattr, go to next entry */
			if (strncmp("x:", ptr, 2) != 0) {
				ptr += kds[i].kd_key_len;
				continue;
			}

			/** set akey as the xattr name */
			d_iov_set(&iod.iod_name, ptr, kds[i].kd_key_len);
			d_iov_set(&fiov, val_buf, DFS_MAX_XATTR_LEN);

			/** fetch the xattr value from the src */
			rc = daos_obj_fetch(src_oh, th, 0, &src_dkey, 1,
					    &iod, &fsgl, NULL, NULL);
			if (rc) {
				D_ERROR("daos_obj_fetch() failed (%d)\n", rc);
				D_GOTO(out, rc = daos_der2errno(rc));
			}

			d_iov_set(&fiov, val_buf, iod.iod_size);

			/** add it to the destination */
			rc = daos_obj_update(dst_oh, th, 0, &dst_dkey, 1,
					     &iod, &fsgl, NULL);
			if (rc) {
				D_ERROR("daos_obj_update() failed (%d)\n", rc);
				D_GOTO(out, rc = daos_der2errno(rc));
			}
			ptr += kds[i].kd_key_len;
		}
	}

out:
	D_FREE(val_buf);
	return rc;
}

/* Returns oids for both moved and clobbered files, but does not check either of them */
int
dfs_move_internal(dfs_t *dfs, unsigned int flags, dfs_obj_t *parent, const char *name,
		  dfs_obj_t *new_parent, const char *new_name, daos_obj_id_t *moid,
		  daos_obj_id_t *oid)
{
	struct dfs_entry	entry = {0}, new_entry = {0};
	daos_handle_t		th = DAOS_TX_NONE;
	bool			exists;
	daos_key_t		dkey;
	size_t			len;
	size_t			new_len;
	int			rc;

	if (dfs == NULL || !dfs->mounted)
		return EINVAL;
	if (dfs->amode != O_RDWR)
		return EPERM;
	if (parent == NULL)
		parent = &dfs->root;
	else if (!S_ISDIR(parent->mode))
		return ENOTDIR;
	if (new_parent == NULL)
		new_parent = &dfs->root;
	else if (!S_ISDIR(new_parent->mode))
		return ENOTDIR;

	if (flags != 0) {
#ifdef RENAME_NOREPLACE
		if (flags != RENAME_NOREPLACE)
			return ENOTSUP;
#else
		return ENOTSUP;
#endif
	}

	rc = check_name(name, &len);
	if (rc)
		return rc;
	rc = check_name(new_name, &new_len);
	if (rc)
		return rc;

	/*
	 * TODO - more permission checks for source & target attributes needed
	 * (immutable, append).
	 */

	if (dfs->use_dtx) {
		rc = daos_tx_open(dfs->coh, &th, 0, NULL);
		if (rc) {
			D_ERROR("daos_tx_open() failed (%d)\n", rc);
			D_GOTO(out, rc = daos_der2errno(rc));
		}
	}

restart:
	rc = fetch_entry(dfs->layout_v, parent->oh, th, name, len, true, &exists, &entry, 0,
			 NULL, NULL, NULL);
	if (rc) {
		D_ERROR("Failed to fetch entry %s (%d)\n", name, rc);
		D_GOTO(out, rc);
	}
	if (exists == false)
		D_GOTO(out, rc = ENOENT);

	if (moid)
		oid_cp(moid, entry.oid);

	rc = fetch_entry(dfs->layout_v, new_parent->oh, th, new_name, new_len, true, &exists,
			 &new_entry, 0, NULL, NULL, NULL);
	if (rc) {
		D_ERROR("Failed to fetch entry %s (%d)\n", new_name, rc);
		D_GOTO(out, rc);
	}

	if (exists) {
#ifdef RENAME_NOREPLACE
		if (flags & RENAME_NOREPLACE)
			D_GOTO(out, rc = EEXIST);
#endif

		if (S_ISDIR(new_entry.mode)) {
			uint32_t	nr = 0;
			daos_handle_t	oh;

			/** if old entry not a dir, return error */
			if (!S_ISDIR(entry.mode)) {
				D_ERROR("Can't rename non dir over a dir\n");
				D_GOTO(out, rc = EINVAL);
			}

			/** make sure new dir is empty */
			rc = daos_obj_open(dfs->coh, new_entry.oid, DAOS_OO_RW, &oh, NULL);
			if (rc) {
				D_ERROR("daos_obj_open() Failed (%d)\n", rc);
				D_GOTO(out, rc = daos_der2errno(rc));
			}

			rc = get_num_entries(oh, th, &nr, true);
			if (rc) {
				D_ERROR("failed to check dir %s (%d)\n", new_name, rc);
				daos_obj_close(oh, NULL);
				D_GOTO(out, rc);
			}

			rc = daos_obj_close(oh, NULL);
			if (rc) {
				D_ERROR("daos_obj_close() Failed (%d)\n", rc);
				D_GOTO(out, rc = daos_der2errno(rc));
			}

			if (nr != 0)
				D_GOTO(out, rc = ENOTEMPTY);
		}

		rc = remove_entry(dfs, th, new_parent->oh, new_name, new_len, new_entry);
		if (rc) {
			D_ERROR("Failed to remove entry %s (%d)\n", new_name, rc);
			D_GOTO(out, rc);
		}

		if (oid)
			oid_cp(oid, new_entry.oid);
	}

	/** rename symlink */
	if (S_ISLNK(entry.mode)) {
		rc = remove_entry(dfs, th, parent->oh, name, len, entry);
		if (rc) {
			D_ERROR("Failed to remove entry %s (%d)\n", name, rc);
			D_GOTO(out, rc);
		}

		rc = insert_entry(dfs->layout_v, parent->oh, th, new_name, new_len,
				  dfs->use_dtx ? 0 : DAOS_COND_DKEY_INSERT, &entry);
		if (rc)
			D_ERROR("Inserting new entry %s failed (%d)\n", new_name, rc);
		D_GOTO(out, rc);
	}

	struct timespec		now;

	rc = clock_gettime(CLOCK_REALTIME, &now);
	if (rc)
		D_GOTO(out, rc = errno);
	entry.mtime = entry.ctime = now.tv_sec;
	entry.mtime_nano = entry.ctime_nano = now.tv_nsec;

	/** insert old entry in new parent object */
	rc = insert_entry(dfs->layout_v, new_parent->oh, th, new_name, new_len,
			  dfs->use_dtx ? 0 : DAOS_COND_DKEY_INSERT, &entry);
	if (rc) {
		D_ERROR("Inserting entry %s DTX %d failed (%d)\n", new_name, dfs->use_dtx, rc);
		D_GOTO(out, rc);
	}

	/** cp the extended attributes if they exist */
	rc = xattr_copy(parent->oh, name, new_parent->oh, new_name, th);
	if (rc == ERESTART) {
		D_GOTO(out, rc);
	} else if (rc) {
		D_ERROR("Failed to copy extended attributes (%d)\n", rc);
		D_GOTO(out, rc);
	}

	/** remove the old entry from the old parent (just the dkey) */
	d_iov_set(&dkey, (void *)name, len);
	rc = daos_obj_punch_dkeys(parent->oh, th, dfs->use_dtx ? 0 : DAOS_COND_PUNCH, 1, &dkey,
				  NULL);
	if (rc) {
		D_ERROR("Punch entry %s failed (%d)\n", name, rc);
		D_GOTO(out, rc = daos_der2errno(rc));
	}

	if (dfs->use_dtx) {
		rc = daos_tx_commit(th, NULL);
		if (rc) {
			if (rc != -DER_TX_RESTART)
				D_ERROR("daos_tx_commit() failed (%d)\n", rc);
			D_GOTO(out, rc = daos_der2errno(rc));
		}
	}

out:
	rc = check_tx(th, rc);
	if (rc == ERESTART)
		goto restart;

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

/* Wrapper function, only permit oid as a input parameter and return if it has data */
int
dfs_move(dfs_t *dfs, dfs_obj_t *parent, const char *name, dfs_obj_t *new_parent,
	 const char *new_name, daos_obj_id_t *oid)
{
	return dfs_move_internal(dfs, 0, parent, name, new_parent, new_name, NULL, oid);
}

int
dfs_exchange(dfs_t *dfs, dfs_obj_t *parent1, const char *name1, dfs_obj_t *parent2,
	     const char *name2)
{
	struct dfs_entry	entry1 = {0}, entry2 = {0};
	daos_handle_t		th = DAOS_TX_NONE;
	bool			exists;
	daos_key_t		dkey;
	size_t			len1;
	size_t			len2;
	int			rc;

	if (dfs == NULL || !dfs->mounted)
		return EINVAL;
	if (dfs->amode != O_RDWR)
		return EPERM;
	if (parent1 == NULL)
		parent1 = &dfs->root;
	else if (!S_ISDIR(parent1->mode))
		return ENOTDIR;
	if (parent2 == NULL)
		parent2 = &dfs->root;
	else if (!S_ISDIR(parent2->mode))
		return ENOTDIR;

	rc = check_name(name1, &len1);
	if (rc)
		return rc;
	rc = check_name(name2, &len2);
	if (rc)
		return rc;

	if (dfs->use_dtx) {
		rc = daos_tx_open(dfs->coh, &th, 0, NULL);
		if (rc) {
			D_ERROR("daos_tx_open() failed (%d)\n", rc);
			D_GOTO(out, rc = daos_der2errno(rc));
		}
	}

restart:
	rc = fetch_entry(dfs->layout_v, parent1->oh, th, name1, len1, true, &exists, &entry1,
			 0, NULL, NULL, NULL);
	if (rc) {
		D_ERROR("Failed to fetch entry %s (%d)\n", name1, rc);
		D_GOTO(out, rc);
	}
	if (exists == false)
		D_GOTO(out, rc = EINVAL);

	rc = fetch_entry(dfs->layout_v, parent2->oh, th, name2, len2, true, &exists, &entry2,
			 0, NULL, NULL, NULL);
	if (rc) {
		D_ERROR("Failed to fetch entry %s (%d)\n", name2, rc);
		D_GOTO(out, rc);
	}

	if (exists == false)
		D_GOTO(out, rc = EINVAL);

	/** remove the first entry from parent1 (just the dkey) */
	d_iov_set(&dkey, (void *)name1, len1);
	rc = daos_obj_punch_dkeys(parent1->oh, th, 0, 1, &dkey, NULL);
	if (rc) {
		D_ERROR("Punch entry %s failed (%d)\n", name1, rc);
		D_GOTO(out, rc = daos_der2errno(rc));
	}

	/** remove the second entry from parent2 (just the dkey) */
	d_iov_set(&dkey, (void *)name2, len2);
	rc = daos_obj_punch_dkeys(parent2->oh, th, 0, 1, &dkey, NULL);
	if (rc) {
		D_ERROR("Punch entry %s failed (%d)\n", name2, rc);
		D_GOTO(out, rc = daos_der2errno(rc));
	}

	struct timespec		now;

	rc = clock_gettime(CLOCK_REALTIME, &now);
	if (rc)
		D_GOTO(out, rc = errno);
	entry1.mtime = entry1.ctime = now.tv_sec;
	entry1.mtime_nano = entry1.ctime_nano = now.tv_nsec;

	/** insert entry1 in parent2 object */
	rc = insert_entry(dfs->layout_v, parent2->oh, th, name1, len1,
			  dfs->use_dtx ? 0 : DAOS_COND_DKEY_INSERT, &entry1);
	if (rc) {
		D_ERROR("Inserting entry %s failed (%d)\n", name1, rc);
		D_GOTO(out, rc);
	}

	entry2.mtime = entry2.ctime = now.tv_sec;
	entry2.mtime_nano = entry2.ctime_nano = now.tv_nsec;

	/** insert entry2 in parent1 object */
	rc = insert_entry(dfs->layout_v, parent1->oh, th, name2, len2,
			  dfs->use_dtx ? 0 : DAOS_COND_DKEY_INSERT, &entry2);
	if (rc) {
		D_ERROR("Inserting entry %s failed (%d)\n", name2, rc);
		D_GOTO(out, rc);
	}

	if (dfs->use_dtx) {
		rc = daos_tx_commit(th, NULL);
		if (rc) {
			if (rc != -DER_TX_RESTART)
				D_ERROR("daos_tx_commit() failed (%d)\n", rc);
			D_GOTO(out, rc = daos_der2errno(rc));
		}
	}

out:
	rc = check_tx(th, rc);
	if (rc == ERESTART)
		goto restart;

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
		return EINVAL;
	if (dfs->amode != O_RDWR)
		return EPERM;

	/** Take a snapshot here and allow rollover to that when supported. */

	return 0;
}

int
dfs_setxattr(dfs_t *dfs, dfs_obj_t *obj, const char *name,
	     const void *value, daos_size_t size, int flags)
{
	char		*xname = NULL;
	daos_handle_t	th = DAOS_TX_NONE;
	d_sg_list_t	sgls[2];
	d_iov_t		sg_iovs[3];
	daos_iod_t	iods[2];
	daos_recx_t	recxs[2];
	daos_key_t	dkey;
	daos_handle_t	oh;
	uint64_t	cond = 0;
	struct timespec	now;
	int		rc;

	if (dfs == NULL || !dfs->mounted)
		return EINVAL;
	if (dfs->amode != O_RDWR)
		return EPERM;
	if (obj == NULL)
		return EINVAL;
	if (name == NULL)
		return EINVAL;
	if (strnlen(name, DFS_MAX_XATTR_NAME + 1) > DFS_MAX_XATTR_NAME)
		return EINVAL;
	if (size > DFS_MAX_XATTR_LEN)
		return EINVAL;

	/** prefix name with x: to avoid collision with internal attrs */
	xname = concat("x:", name);
	if (xname == NULL)
		return ENOMEM;

	/** Open parent object and insert xattr in the entry of the object */
	rc = daos_obj_open(dfs->coh, obj->parent_oid, DAOS_OO_RW, &oh, NULL);
	if (rc)
		D_GOTO(free, rc = daos_der2errno(rc));

	/** set dkey as the entry name */
	d_iov_set(&dkey, (void *)obj->name, strlen(obj->name));

	/** add xattr iod & sgl */
	d_iov_set(&iods[0].iod_name, xname, strlen(xname));
	iods[0].iod_nr	= 1;
	iods[0].iod_recxs	= NULL;
	iods[0].iod_type	= DAOS_IOD_SINGLE;
	iods[0].iod_size	= size;
	d_iov_set(&sg_iovs[0], (void *)value, size);
	sgls[0].sg_nr		= 1;
	sgls[0].sg_nr_out	= 0;
	sgls[0].sg_iovs		= &sg_iovs[0];

	/** add ctime iod & sgl */
	d_iov_set(&iods[1].iod_name, INODE_AKEY_NAME, sizeof(INODE_AKEY_NAME) - 1);
	iods[1].iod_recxs	= recxs;
	iods[1].iod_type	= DAOS_IOD_ARRAY;
	iods[1].iod_size	= 1;
	iods[1].iod_nr		= 2;
	recxs[0].rx_idx		= CTIME_IDX;
	recxs[0].rx_nr		= sizeof(uint64_t);
	recxs[1].rx_idx		= CTIME_NSEC_IDX;
	recxs[1].rx_nr		= sizeof(uint64_t);
	rc = clock_gettime(CLOCK_REALTIME, &now);
	if (rc)
		D_GOTO(out, rc = errno);
	sgls[1].sg_nr		= 2;
	sgls[1].sg_nr_out	= 0;
	sgls[1].sg_iovs		= &sg_iovs[1];
	d_iov_set(&sg_iovs[1], &now.tv_sec, sizeof(uint64_t));
	d_iov_set(&sg_iovs[2], &now.tv_nsec, sizeof(uint64_t));

	/** if not default flag, check for xattr existence */
	if (flags != 0) {
		if (flags == XATTR_CREATE)
			cond |= DAOS_COND_AKEY_INSERT;
		if (flags == XATTR_REPLACE)
			cond |= DAOS_COND_AKEY_UPDATE;
	}
	cond |= DAOS_COND_DKEY_UPDATE;

	/** update ctime in a separate update if DAOS_COND_AKEY_INSERT is used for the xattr */
	if (cond & DAOS_COND_AKEY_INSERT) {
		/** insert the xattr */
		rc = daos_obj_update(oh, th, cond, &dkey, 1, &iods[0], &sgls[0], NULL);
		if (rc) {
			D_ERROR("Failed to insert extended attribute %s\n", name);
			D_GOTO(out, rc = daos_der2errno(rc));
		}
		/** update the ctime */
		rc = daos_obj_update(oh, th, DAOS_COND_DKEY_UPDATE, &dkey, 1, &iods[1], &sgls[1],
				     NULL);
		if (rc) {
			D_ERROR("Failed to update ctime %s\n", name);
			D_GOTO(out, rc = daos_der2errno(rc));
		}
	} else {
		/** replace the xattr and update the ctime */
		rc = daos_obj_update(oh, th, cond, &dkey, 2, iods, sgls, NULL);
		if (rc) {
			D_ERROR("Failed to insert extended attribute %s\n", name);
			D_GOTO(out, rc = daos_der2errno(rc));
		}
	}

out:
	daos_obj_close(oh, NULL);
free:
	D_FREE(xname);
	return rc;
}

int
dfs_getxattr(dfs_t *dfs, dfs_obj_t *obj, const char *name, void *value,
	     daos_size_t *size)
{
	char		*xname = NULL;
	d_sg_list_t	sgl;
	d_iov_t		sg_iov;
	daos_iod_t	iod;
	daos_key_t	dkey;
	daos_handle_t	oh;
	int		rc;

	if (dfs == NULL || !dfs->mounted)
		return EINVAL;
	if (obj == NULL)
		return EINVAL;
	if (name == NULL)
		return EINVAL;
	if (strnlen(name, DFS_MAX_XATTR_NAME + 1) > DFS_MAX_XATTR_NAME)
		return EINVAL;

	xname = concat("x:", name);
	if (xname == NULL)
		return ENOMEM;

	/** Open parent object and get xattr from the entry of the object */
	rc = daos_obj_open(dfs->coh, obj->parent_oid, DAOS_OO_RO, &oh, NULL);
	if (rc)
		D_GOTO(out, rc = daos_der2errno(rc));

	/** set dkey as the entry name */
	d_iov_set(&dkey, (void *)obj->name, strlen(obj->name));

	/** set akey as the xattr name */
	d_iov_set(&iod.iod_name, xname, strlen(xname));
	iod.iod_nr	= 1;
	iod.iod_recxs	= NULL;
	iod.iod_type	= DAOS_IOD_SINGLE;

	if (*size) {
		iod.iod_size	= *size;

		/** set sgl for fetch */
		d_iov_set(&sg_iov, value, *size);
		sgl.sg_nr	= 1;
		sgl.sg_nr_out	= 0;
		sgl.sg_iovs	= &sg_iov;

		rc = daos_obj_fetch(oh, DAOS_TX_NONE, DAOS_COND_AKEY_FETCH, &dkey, 1,
				    &iod, &sgl, NULL, NULL);
	} else {
		iod.iod_size	= DAOS_REC_ANY;

		rc = daos_obj_fetch(oh, DAOS_TX_NONE, DAOS_COND_AKEY_FETCH, &dkey, 1,
				    &iod, NULL, NULL, NULL);
	}
	if (rc) {
		D_CDEBUG(rc == -DER_NONEXIST, DLOG_DBG, DLOG_ERR,
			 "Failed to fetch xattr '%s' " DF_RC "\n", name, DP_RC(rc));
		D_GOTO(close, rc = daos_der2errno(rc));
	}

	*size = iod.iod_size;

close:
	daos_obj_close(oh, NULL);
out:
	D_FREE(xname);
	if (rc == ENOENT)
		rc = ENODATA;
	return rc;
}

int
dfs_removexattr(dfs_t *dfs, dfs_obj_t *obj, const char *name)
{
	char		*xname = NULL;
	daos_handle_t	th = DAOS_TX_NONE;
	daos_key_t	dkey, akey;
	daos_handle_t	oh;
	uint64_t	cond = 0;
	d_sg_list_t	sgl;
	d_iov_t		sg_iovs[2];
	daos_iod_t	iod;
	daos_recx_t	recxs[2];
	struct timespec	now;
	int		rc;

	if (dfs == NULL || !dfs->mounted)
		return EINVAL;
	if (dfs->amode != O_RDWR)
		return EPERM;
	if (obj == NULL)
		return EINVAL;
	if (name == NULL)
		return EINVAL;
	if (strnlen(name, DFS_MAX_XATTR_NAME + 1) > DFS_MAX_XATTR_NAME)
		return EINVAL;

	xname = concat("x:", name);
	if (xname == NULL)
		return ENOMEM;

	/** Open parent object and remove xattr from the entry of the object */
	rc = daos_obj_open(dfs->coh, obj->parent_oid, DAOS_OO_RW, &oh, NULL);
	if (rc)
		D_GOTO(free, rc = daos_der2errno(rc));

	/** set dkey as the entry name */
	d_iov_set(&dkey, (void *)obj->name, strlen(obj->name));
	/** set akey as the xattr name */
	d_iov_set(&akey, xname, strlen(xname));

	cond = DAOS_COND_DKEY_UPDATE | DAOS_COND_PUNCH;
	rc = daos_obj_punch_akeys(oh, th, cond, &dkey, 1, &akey, NULL);
	if (rc) {
		D_CDEBUG(rc == -DER_NONEXIST, DLOG_DBG, DLOG_ERR,
			 "Failed to punch extended attribute '%s'\n", name);
		D_GOTO(out, rc = daos_der2errno(rc));
	}

	/** update ctime */
	d_iov_set(&iod.iod_name, INODE_AKEY_NAME, sizeof(INODE_AKEY_NAME) - 1);
	iod.iod_recxs	= recxs;
	iod.iod_type	= DAOS_IOD_ARRAY;
	iod.iod_size	= 1;
	iod.iod_nr	= 2;
	recxs[0].rx_idx	= CTIME_IDX;
	recxs[0].rx_nr	= sizeof(uint64_t);
	recxs[1].rx_idx	= CTIME_NSEC_IDX;
	recxs[1].rx_nr	= sizeof(uint64_t);
	rc = clock_gettime(CLOCK_REALTIME, &now);
	if (rc)
		D_GOTO(out, rc = errno);
	sgl.sg_nr	= 2;
	sgl.sg_nr_out	= 0;
	sgl.sg_iovs	= &sg_iovs[0];
	d_iov_set(&sg_iovs[0], &now.tv_sec, sizeof(uint64_t));
	d_iov_set(&sg_iovs[1], &now.tv_nsec, sizeof(uint64_t));

	rc = daos_obj_update(oh, th, DAOS_COND_DKEY_UPDATE, &dkey, 1, &iod, &sgl, NULL);
	if (rc) {
		D_ERROR("Failed to update mode, "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc = daos_der2errno(rc));
	}

out:
	daos_obj_close(oh, NULL);
free:
	D_FREE(xname);
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
		return EINVAL;
	if (obj == NULL)
		return EINVAL;

	/** Open parent object and list from entry */
	rc = daos_obj_open(dfs->coh, obj->parent_oid, DAOS_OO_RW, &oh, NULL);
	if (rc)
		return daos_der2errno(rc);

	/** set dkey as the entry name */
	d_iov_set(&dkey, (void *)obj->name, strlen(obj->name));

	list_size = *size;
	ret_size = 0;
	ptr_list = list;

	while (!daos_anchor_is_eof(&anchor)) {
		uint32_t	number = ENUM_DESC_NR;
		uint32_t	i;
		d_iov_t		iov;
		char		enum_buf[ENUM_XDESC_BUF] = {0};
		d_sg_list_t	sgl;
		char		*ptr;

		sgl.sg_nr = 1;
		sgl.sg_nr_out = 0;
		d_iov_set(&iov, enum_buf, ENUM_DESC_BUF);
		sgl.sg_iovs = &iov;

		rc = daos_obj_list_akey(oh, DAOS_TX_NONE, &dkey, &number, kds,
					&sgl, &anchor, NULL);
		if (rc)
			D_GOTO(out, rc = daos_der2errno(rc));

		if (number == 0)
			continue;

		for (ptr = enum_buf, i = 0; i < number; i++) {
			int len;

			if (strncmp("x:", ptr, 2) != 0) {
				ptr += kds[i].kd_key_len;
				continue;
			}

			ret_size += kds[i].kd_key_len - 1;

			if (list == NULL)
				continue;
			if (list_size < kds[i].kd_key_len - 2)
				continue;

			len = snprintf(ptr_list, kds[i].kd_key_len - 1, "%s",
				       ptr + 2);
			D_ASSERT(len >= kds[i].kd_key_len - 2);

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
	if (obj == NULL || oid == NULL)
		return EINVAL;
	oid_cp(oid, obj->oid);
	return 0;
}

int
dfs_obj_anchor_split(dfs_obj_t *obj, uint32_t *nr, daos_anchor_t *anchors)
{
	int rc;

	if (obj == NULL || nr == NULL || !S_ISDIR(obj->mode))
		return EINVAL;

	rc = daos_obj_anchor_split(obj->oh, nr, anchors);
	return daos_der2errno(rc);
}

int
dfs_obj_anchor_set(dfs_obj_t *obj, uint32_t index, daos_anchor_t *anchor)
{
	int rc;

	if (obj == NULL || !S_ISDIR(obj->mode))
		return EINVAL;

	rc = daos_obj_anchor_set(obj->oh, index, anchor);
	return daos_der2errno(rc);
}

int
dfs_dir_anchor_set(dfs_obj_t *obj, const char name[], daos_anchor_t *anchor)
{
	daos_key_t	dkey;
	size_t		len;
	int		rc;

	if (obj == NULL || !S_ISDIR(obj->mode))
		return EINVAL;
	rc = check_name(name, &len);
	if (rc)
		return rc;

	d_iov_set(&dkey, (void *)name, len);
	rc = daos_obj_key2anchor(obj->oh, DAOS_TX_NONE, &dkey, NULL, anchor, NULL);
	return daos_der2errno(rc);
}

#define DFS_ITER_NR		128
#define DFS_ITER_DKEY_BUF	(DFS_ITER_NR * sizeof(uint64_t))
#define DFS_ITER_ENTRY_BUF	(DFS_ITER_NR * DFS_MAX_NAME)
#define DFS_ELAPSED_TIME	30

struct dfs_oit_args {
	daos_handle_t	oit;
	uint64_t	flags;
	uint64_t	snap_epoch;
	uint64_t	skipped;
	uint64_t	failed;
	time_t		start_time;
	time_t		print_time;
	uint64_t	num_scanned;
};

static int
fetch_mark_oids(daos_handle_t coh, daos_obj_id_t oid, daos_key_desc_t *kds, char *enum_buf,
		struct dfs_oit_args *args)
{
	daos_handle_t	oh;
	d_sg_list_t	sgl, entry_sgl;
	d_iov_t		iov, sg_iov;
	daos_recx_t	recx;
	uint32_t	nr;
	daos_iod_t	iod;
	d_iov_t		marker;
	bool		mark_data = true;
	daos_anchor_t	anchor = {0};
	int		rc, rc2;

	rc = daos_obj_open(coh, oid, DAOS_OO_RW, &oh, NULL);
	if (rc) {
		D_ERROR("daos_obj_open() failed "DF_RC"\n", DP_RC(rc));
		return daos_der2errno(rc);
	}

	/** set sgl for enumeration */
	sgl.sg_nr = 1;
	sgl.sg_nr_out = 0;
	d_iov_set(&iov, enum_buf, DFS_ITER_ENTRY_BUF);
	sgl.sg_iovs = &iov;

	/** set sgl for fetch */
	entry_sgl.sg_nr = 1;
	entry_sgl.sg_nr_out = 0;
	entry_sgl.sg_iovs = &sg_iov;

	d_iov_set(&iod.iod_name, INODE_AKEY_NAME, sizeof(INODE_AKEY_NAME) - 1);
	recx.rx_idx	= OID_IDX;
	recx.rx_nr	= sizeof(daos_obj_id_t);
	iod.iod_nr	= 1;
	iod.iod_recxs	= &recx;
	iod.iod_type	= DAOS_IOD_ARRAY;
	iod.iod_size	= 1;

	d_iov_set(&marker, &mark_data, sizeof(mark_data));
	while (!daos_anchor_is_eof(&anchor)) {
		uint32_t	i;
		char		*ptr;

		nr = DFS_ITER_NR;
		rc = daos_obj_list_dkey(oh, DAOS_TX_NONE, &nr, kds, &sgl, &anchor, NULL);
		if (rc) {
			D_ERROR("daos_obj_list_dkey() failed "DF_RC"\n", DP_RC(rc));
			D_GOTO(out_obj, rc = daos_der2errno(rc));
		}

		/** for every entry, fetch its oid and mark it in the oit table */
		for (ptr = enum_buf, i = 0; i < nr; i++) {
			daos_obj_id_t	entry_oid;
			daos_key_t	dkey;

			d_iov_set(&dkey, ptr, kds[i].kd_key_len);
			d_iov_set(&sg_iov, &entry_oid, sizeof(daos_obj_id_t));

			rc = daos_obj_fetch(oh, DAOS_TX_NONE, DAOS_COND_DKEY_FETCH, &dkey, 1, &iod,
					    &entry_sgl, NULL, NULL);
			if (rc) {
				D_ERROR("daos_obj_fetch() failed "DF_RC"\n", DP_RC(rc));
				D_GOTO(out_obj, rc = daos_der2errno(rc));
			}

			/** mark oid in the oit table */
			rc = daos_oit_mark(args->oit, entry_oid, &marker, NULL);
			if (rc && rc != -DER_NONEXIST) {
				D_ERROR("daos_oit_mark() failed "DF_RC"\n", DP_RC(rc));
				D_GOTO(out_obj, rc = daos_der2errno(rc));
			}
			rc = 0;
			ptr += kds[i].kd_key_len;
		}
	}

out_obj:
	rc2 = daos_obj_close(oh, NULL);
	if (rc == 0)
		rc = rc2;
	return rc;
}

static int
oit_mark_cb(dfs_t *dfs, dfs_obj_t *parent, const char name[], void *args)
{
	struct dfs_oit_args	*oit_args = (struct dfs_oit_args *)args;
	dfs_obj_t		*obj;
	daos_obj_id_t		oid;
	d_iov_t			marker;
	bool			mark_data = true;
	struct timespec		current_time;
	int			rc;

	rc = clock_gettime(CLOCK_REALTIME, &current_time);
	if (rc)
		return errno;
	oit_args->num_scanned ++;
	if (current_time.tv_sec - oit_args->print_time >= DFS_ELAPSED_TIME) {
		D_PRINT("DFS checker: Scanned "DF_U64" files/directories (runtime: "DF_U64" sec)\n",
			oit_args->num_scanned, current_time.tv_sec - oit_args->start_time);
		oit_args->print_time = current_time.tv_sec;
	}

	/** open the entry name and get the oid */
	rc = dfs_lookup_rel(dfs, parent, name, O_RDONLY, &obj, NULL, NULL);
	if (rc) {
		D_ERROR("dfs_lookup_rel() of %s failed: %d\n", name, rc);
		return rc;
	}

	rc = dfs_obj2id(obj, &oid);
	if (rc)
		D_GOTO(out_obj, rc);

	if (oit_args->flags & DFS_CHECK_VERIFY) {
		rc = daos_obj_verify(dfs->coh, oid, oit_args->snap_epoch);
		if (rc == -DER_NOSYS) {
			oit_args->skipped++;
		} else if (rc == -DER_MISMATCH) {
			oit_args->failed++;
			if (oit_args->flags & DFS_CHECK_PRINT)
				D_PRINT(""DF_OID" failed data consistency check!\n", DP_OID(oid));
		} else if (rc) {
			D_ERROR("daos_obj_verify() failed "DF_RC"\n", DP_RC(rc));
			D_GOTO(out_obj, rc = daos_der2errno(rc));
		}
	}

	d_iov_set(&marker, &mark_data, sizeof(mark_data));
	rc = daos_oit_mark(oit_args->oit, oid, &marker, NULL);
	/*
	 * If the entry exists but the file or directory are empty, the corresponding oid itself has
	 * not been written to, so it doesn't exist in the OIT. The mark operation would return
	 * NONEXIST in this case, so check and avoid returning an error in this case.
	 */
	if (rc && rc != -DER_NONEXIST) {
		D_ERROR("Failed to mark OID in OIT: "DF_RC"\n", DP_RC(rc));
		D_GOTO(out_obj, rc = daos_der2errno(rc));
	}

	/** descend into directories */
	if (S_ISDIR(obj->mode))	{
		daos_anchor_t		anchor = {0};
		uint32_t		nr_entries = DFS_ITER_NR;

		while (!daos_anchor_is_eof(&anchor)) {
			rc = dfs_iterate(dfs, obj, &anchor, &nr_entries, DFS_MAX_NAME * nr_entries,
					 oit_mark_cb, args);
			if (rc) {
				D_ERROR("dfs_iterate() failed: %d\n", rc);
				D_GOTO(out_obj, rc);
			}
			nr_entries = DFS_ITER_NR;
		}
	}

out_obj:
	rc = dfs_release(obj);
	return rc;
}

static int
adjust_chunk_size(daos_handle_t coh, daos_obj_id_t oid, daos_key_desc_t *kds, char *enum_buf,
		  uint64_t *_max_offset)
{
	daos_handle_t	oh;
	daos_anchor_t	anchor = {0};
	d_sg_list_t	sgl;
	d_iov_t		iov;
	uint64_t	max_offset = *_max_offset;
	int		rc, rc2;

	rc = daos_obj_open(coh, oid, DAOS_OO_RW, &oh, NULL);
	if (rc) {
		D_ERROR("daos_obj_open() failed "DF_RC"\n", DP_RC(rc));
		return daos_der2errno(rc);
	}

	/** iterate over all (integer) dkeys and then query the max record / offset */
	sgl.sg_nr = 1;
	sgl.sg_nr_out = 0;
	d_iov_set(&iov, enum_buf, DFS_ITER_DKEY_BUF);
	sgl.sg_iovs = &iov;

	while (!daos_anchor_is_eof(&anchor)) {
		uint32_t	nr = DFS_ITER_NR;
		char		*ptr = &enum_buf[0];
		uint32_t	i;

		rc = daos_obj_list_dkey(oh, DAOS_TX_NONE, &nr, kds, &sgl, &anchor, NULL);
		if (rc) {
			D_ERROR("daos_obj_list_dkey() failed "DF_RC"\n", DP_RC(rc));
			D_GOTO(out_obj, rc = daos_der2errno(rc));
		}

		if (nr == 0)
			continue;

		for (i = 0; i < nr; i++) {
			daos_key_t	dkey, akey;
			uint64_t	dkey_val, offset;
			char		akey_val = '0';
			daos_recx_t	recx;

			memcpy(&dkey_val, ptr, kds[i].kd_key_len);
			ptr += kds[i].kd_key_len;
			d_iov_set(&dkey, &dkey_val, sizeof(uint64_t));
			d_iov_set(&akey, &akey_val, 1);
			rc = daos_obj_query_key(oh, DAOS_TX_NONE, DAOS_GET_RECX | DAOS_GET_MAX,
						&dkey, &akey, &recx, NULL);
			if (rc) {
				D_ERROR("daos_obj_query_key() failed "DF_RC"\n", DP_RC(rc));
				D_GOTO(out_obj, rc = daos_der2errno(rc));
			}

			/** maintain the highest offset seen in each dkey */
			offset = recx.rx_idx + recx.rx_nr;
			if (max_offset < offset)
				max_offset = offset;
		}
	}

	*_max_offset = max_offset;
out_obj:
	rc2 = daos_obj_close(oh, NULL);
	if (rc == 0)
		rc = daos_der2errno(rc2);
	return rc;
}

int
dfs_cont_check(daos_handle_t poh, const char *cont, uint64_t flags, const char *name)
{
	dfs_t			*dfs;
	daos_handle_t		coh;
	struct dfs_oit_args	*oit_args = NULL;
	daos_epoch_t		snap_epoch;
	dfs_obj_t		*lf, *now_dir;
	daos_anchor_t		anchor = {0};
	uint32_t		nr_entries = DFS_ITER_NR, i;
	daos_obj_id_t		oids[DFS_ITER_NR] = {0};
	daos_key_desc_t		*kds = NULL;
	char			*dkey_enum_buf = NULL;
	char			*entry_enum_buf = NULL;
	uint64_t		unmarked_entries = 0;
	d_iov_t			marker;
	bool			mark_data = true;
	daos_epoch_range_t	epr;
	struct timespec		now, current_time;
	uid_t			uid = geteuid();
	gid_t			gid = getegid();
	unsigned int		co_flags = DAOS_COO_EX;
	char			now_name[24];
	struct tm		*now_tm;
	daos_size_t		len;
	int			rc, rc2;

	rc = clock_gettime(CLOCK_REALTIME, &now);
	if (rc)
		return errno;
	now_tm = localtime(&now.tv_sec);
	len = strftime(now_name, sizeof(now_name), "%Y-%m-%d-%H:%M:%S", now_tm);
	if (len == 0)
		return EINVAL;
	D_PRINT("DFS checker: Start (%s)\n", now_name);

	if (flags & DFS_CHECK_RELINK && flags & DFS_CHECK_REMOVE) {
		D_ERROR("can't request remove and link to l+f at the same time\n");
		return EINVAL;
	}

	if (flags & DFS_CHECK_EVICT_ALL)
		co_flags |= DAOS_COO_EVICT_ALL;

	rc = daos_cont_open(poh, cont, co_flags, &coh, NULL, NULL);
	if (rc) {
		D_ERROR("daos_cont_open() failed: " DF_RC "\n", DP_RC(rc));
		return daos_der2errno(rc);
	}

	rc = dfs_mount(poh, coh, O_RDWR, &dfs);
	if (rc) {
		D_ERROR("dfs_mount() failed (%d)\n", rc);
		D_GOTO(out_cont, rc);
	}

	D_PRINT("DFS checker: Create OIT table\n");
	/** create snapshot for OIT */
	rc = daos_cont_create_snap_opt(coh, &snap_epoch, NULL, DAOS_SNAP_OPT_CR | DAOS_SNAP_OPT_OIT,
				       NULL);
	if (rc) {
		D_ERROR("daos_cont_create_snap_opt failed "DF_RC"\n", DP_RC(rc));
		D_GOTO(out_dfs, rc = daos_der2errno(rc));
	}

	D_ALLOC_PTR(oit_args);
	if (oit_args == NULL)
		D_GOTO(out_dfs, rc = ENOMEM);
	oit_args->flags = flags;
	oit_args->snap_epoch = snap_epoch;
	oit_args->start_time = now.tv_sec;
	oit_args->print_time = now.tv_sec;

	/** Open OIT table */
	rc = daos_oit_open(coh, snap_epoch, &oit_args->oit, NULL);
	if (rc) {
		D_ERROR("daos_oit_open failed "DF_RC"\n", DP_RC(rc));
		D_GOTO(out_snap, rc = daos_der2errno(rc));
	}

	/** get and mark the SB and root OIDs */
	d_iov_set(&marker, &mark_data, sizeof(mark_data));
	rc = daos_oit_mark(oit_args->oit, dfs->super_oid, &marker, NULL);
	if (rc) {
		D_ERROR("Failed to mark SB OID in OIT: "DF_RC"\n", DP_RC(rc));
		D_GOTO(out_oit, rc = daos_der2errno(rc));
	}
	rc = daos_oit_mark(oit_args->oit, dfs->root.oid, &marker, NULL);
	if (rc && rc != -DER_NONEXIST) {
		D_ERROR("Failed to mark ROOT OID in OIT: "DF_RC"\n", DP_RC(rc));
		D_GOTO(out_oit, rc = daos_der2errno(rc));
	}
	rc = 0;

	if (flags & DFS_CHECK_VERIFY) {
		rc = daos_obj_verify(coh, dfs->super_oid, snap_epoch);
		if (rc == -DER_NOSYS) {
			oit_args->skipped++;
		} else if (rc == -DER_MISMATCH) {
			oit_args->failed++;
			if (flags & DFS_CHECK_PRINT)
				D_PRINT("SB Object "DF_OID" failed data consistency check!\n",
					DP_OID(dfs->super_oid));
		} else if (rc) {
			D_ERROR("daos_obj_verify() failed "DF_RC"\n", DP_RC(rc));
			D_GOTO(out_oit, rc = daos_der2errno(rc));
		}

		rc = daos_obj_verify(coh, dfs->root.oid, snap_epoch);
		if (rc == -DER_NOSYS) {
			oit_args->skipped++;
		} else if (rc == -DER_MISMATCH) {
			oit_args->failed++;
			if (flags & DFS_CHECK_PRINT)
				D_PRINT("ROOT Object "DF_OID" failed data consistency check!\n",
					DP_OID(dfs->root.oid));
		} else if (rc) {
			D_ERROR("daos_obj_verify() failed "DF_RC"\n", DP_RC(rc));
			D_GOTO(out_oit, rc = daos_der2errno(rc));
		}
	}

	D_PRINT("DFS checker: Iterating namespace and marking objects\n");
	oit_args->num_scanned = 2;
	/** iterate through the namespace and mark OITs starting from the root object */
	while (!daos_anchor_is_eof(&anchor)) {
		rc = dfs_iterate(dfs, &dfs->root, &anchor, &nr_entries, DFS_MAX_NAME * nr_entries,
				 oit_mark_cb, oit_args);
		if (rc) {
			D_ERROR("dfs_iterate() failed: %d\n", rc);
			D_GOTO(out_oit, rc);
		}

		nr_entries = DFS_ITER_NR;
	}

	rc = clock_gettime(CLOCK_REALTIME, &current_time);
	if (rc)
		D_GOTO(out_oit, rc = errno);
	D_PRINT("DFS checker: marked "DF_U64" files/directories (runtime: "DF_U64" sec))\n",
		oit_args->num_scanned, current_time.tv_sec - oit_args->start_time);

	/** Create lost+found directory to link unmarked oids there. */
	if (flags & DFS_CHECK_RELINK) {
		rc = dfs_open(dfs, NULL, "lost+found", S_IFDIR | 0755, O_CREAT | O_RDWR, 0, 0, NULL,
			      &lf);
		if (rc) {
			D_ERROR("Failed to create/open lost+found directory: %d\n", rc);
			D_GOTO(out_oit, rc);
		}

		if (name == NULL) {
			/*
			 * Create a directory with current timestamp in l+f where leaked oids will
			 * be linked in this run.
			 */
			D_PRINT("DFS checker: Leaked OIDs will be inserted in /lost+found/%s\n",
				now_name);
		} else {
			D_PRINT("DFS checker: Leaked OIDs will be inserted in /lost+found/%s\n",
				name);
		}

		rc = dfs_open(dfs, lf, name ? name : now_name, S_IFDIR | 0755,
			      O_CREAT | O_RDWR | O_EXCL, 0, 0, NULL, &now_dir);
		if (rc) {
			D_ERROR("Failed to create dir in lost+found: %d\n", rc);
			D_GOTO(out_lf1, rc);
		}

		/** allocate kds and enumeration buffers */
		D_ALLOC_ARRAY(kds, DFS_ITER_NR);
		if (kds == NULL)
			D_GOTO(out_lf2, rc = ENOMEM);

		/** Allocate a buffer to store the array int dkeys */
		D_ALLOC_ARRAY(dkey_enum_buf, DFS_ITER_DKEY_BUF);
		if (dkey_enum_buf == NULL)
			D_GOTO(out_lf2, rc = ENOMEM);

		/** Allocate a buffer to store the entries */
		D_ALLOC_ARRAY(entry_enum_buf, DFS_ITER_ENTRY_BUF);
		if (entry_enum_buf == NULL)
			D_GOTO(out_lf2, rc = ENOMEM);
	}

	/*
	 * list all unmarked oids and print / remove / punch. In the case of the L+F relink flag, we
	 * need 2 passes instead of 1:
	 * Pass 1: check directories only and descend to mark all oids in the namespace of each dir.
	 * Pass 2: relink remaining oids in the L+F root that are unmarked still after first pass.
	 */
	D_PRINT("DFS checker: Checking unmarked OIDs (Pass 1)\n");
	oit_args->num_scanned = 0;
	memset(&anchor, 0, sizeof(anchor));
	/** Start Pass 1 */
	while (!daos_anchor_is_eof(&anchor)) {
		nr_entries = DFS_ITER_NR;
		rc = daos_oit_list_unmarked(oit_args->oit, oids, &nr_entries, &anchor, NULL);
		if (rc) {
			D_ERROR("daos_oit_list_unmarked() failed: "DF_RC"\n", DP_RC(rc));
			D_GOTO(out_lf2, rc = daos_der2errno(rc));
		}

		clock_gettime(CLOCK_REALTIME, &current_time);
		if (rc)
			D_GOTO(out_lf2, rc = errno);
		oit_args->num_scanned += nr_entries;
		if (current_time.tv_sec - oit_args->print_time >= DFS_ELAPSED_TIME) {
			D_PRINT("DFS checker: Checked "DF_U64" objects (runtime: "DF_U64" sec)\n",
				oit_args->num_scanned, current_time.tv_sec - oit_args->start_time);
			oit_args->print_time = current_time.tv_sec;
		}

		for (i = 0; i < nr_entries; i++) {
			if (flags & DFS_CHECK_RELINK) {
				enum daos_otype_t otype = daos_obj_id2type(oids[i]);

				/** Pass 1 - if a file is seen, skip in this pass */
				if (daos_is_array_type(otype))
					continue;

				/** for a directory, mark the oids reachable from it */
				rc = fetch_mark_oids(coh, oids[i], kds, entry_enum_buf, oit_args);
				if (rc)
					D_GOTO(out_lf2, rc);
				continue;
			}

			if (flags & DFS_CHECK_PRINT)
				D_PRINT("oid["DF_U64"]: "DF_OID"\n", unmarked_entries,
					DP_OID(oids[i]));

			if (flags & DFS_CHECK_VERIFY) {
				rc = daos_obj_verify(dfs->coh, oids[i], snap_epoch);
				if (rc == -DER_NOSYS) {
					oit_args->skipped++;
				} else if (rc == -DER_MISMATCH) {
					oit_args->failed++;
					if (flags & DFS_CHECK_PRINT)
						D_PRINT(""DF_OID" failed data consistency check!\n",
							DP_OID(oids[i]));
				} else if (rc) {
					D_ERROR("daos_obj_verify() failed "DF_RC"\n", DP_RC(rc));
					D_GOTO(out_oit, rc = daos_der2errno(rc));
				}
			}

			if (flags & DFS_CHECK_REMOVE) {
				daos_handle_t oh;

				rc = daos_obj_open(dfs->coh, oids[i], DAOS_OO_RW, &oh, NULL);
				if (rc)
					D_GOTO(out_oit, rc = daos_der2errno(rc));

				rc = daos_obj_punch(oh, DAOS_TX_NONE, 0, NULL);
				if (rc) {
					daos_obj_close(oh, NULL);
					D_GOTO(out_oit, rc = daos_der2errno(rc));
				}

				rc = daos_obj_close(oh, NULL);
				if (rc)
					D_GOTO(out_oit, rc = daos_der2errno(rc));
			}

			unmarked_entries++;
		}
	}

	/** Start Pass 2 only if L+F flag is used */
	if (!(flags & DFS_CHECK_RELINK))
		goto done;

	D_PRINT("DFS checker: Checking unmarked OIDs (Pass 2)\n");
	oit_args->num_scanned = 0;
	memset(&anchor, 0, sizeof(anchor));
	while (!daos_anchor_is_eof(&anchor)) {
		nr_entries = DFS_ITER_NR;
		rc = daos_oit_list_unmarked(oit_args->oit, oids, &nr_entries, &anchor, NULL);
		if (rc) {
			D_ERROR("daos_oit_list_unmarked() failed: "DF_RC"\n", DP_RC(rc));
			D_GOTO(out_lf2, rc = daos_der2errno(rc));
		}

		clock_gettime(CLOCK_REALTIME, &current_time);
		if (rc)
			D_GOTO(out_lf2, rc = errno);
		oit_args->num_scanned += nr_entries;
		if (current_time.tv_sec - oit_args->print_time >= DFS_ELAPSED_TIME) {
			D_PRINT("DFS checker: Checked "DF_U64" objects (runtime: "DF_U64" sec)\n",
				oit_args->num_scanned, current_time.tv_sec - oit_args->start_time);
			oit_args->print_time = current_time.tv_sec;
		}

		for (i = 0; i < nr_entries; i++) {
			struct dfs_entry	entry = {0};
			enum daos_otype_t	otype = daos_obj_id2type(oids[i]);
			char			oid_name[DFS_MAX_NAME + 1];

			if (flags & DFS_CHECK_PRINT)
				D_PRINT("oid["DF_U64"]: "DF_OID"\n", unmarked_entries,
					DP_OID(oids[i]));

			if (flags & DFS_CHECK_VERIFY) {
				rc = daos_obj_verify(dfs->coh, oids[i], snap_epoch);
				if (rc == -DER_NOSYS) {
					oit_args->skipped++;
				} else if (rc == -DER_MISMATCH) {
					oit_args->failed++;
					if (flags & DFS_CHECK_PRINT)
						D_PRINT(""DF_OID" failed data consistency check!\n",
							DP_OID(oids[i]));
				} else if (rc) {
					D_ERROR("daos_obj_verify() failed "DF_RC"\n", DP_RC(rc));
					D_GOTO(out_lf2, rc = daos_der2errno(rc));
				}
			}

			if (daos_is_array_type(otype))
				entry.mode = S_IFREG | 0600;
			else
				entry.mode = S_IFDIR | 0700;
			entry.uid = uid;
			entry.gid = gid;
			oid_cp(&entry.oid, oids[i]);
			entry.mtime = entry.ctime = now.tv_sec;
			entry.mtime_nano = entry.ctime_nano = now.tv_nsec;
			entry.chunk_size = dfs->attr.da_chunk_size;

			/*
			 * If this is a regular file / array object, the user might have used a
			 * different chunk size than the default one. Since we lost the directory
			 * entry where the chunks size for that file would have been stored, we can
			 * make a best attempt to set the chunk size. To do that, we get the largest
			 * offset of every dkey in the array to see if it's larger than the default
			 * chunk size. If it is, adjust the chunk size to that, otherwise leave the
			 * chunk size as default. This of course does not account for the fact that
			 * if the chunk size is smaller than the default or is larger than the
			 * largest offset seen then we have added or removed existing holes from the
			 * file respectively. This can be fixed later by the user by adjusting the
			 * chunk size to the correct one they know using the daos tool.
			 */
			if (daos_is_array_type(otype)) {
				rc = adjust_chunk_size(dfs->coh, oids[i], kds, dkey_enum_buf,
						       &entry.chunk_size);
				if (rc)
					D_GOTO(out_lf2, rc);
				if (flags & DFS_CHECK_PRINT &&
				    entry.chunk_size != dfs->attr.da_chunk_size)
					D_PRINT("Adjusting File ("DF_OID") chunk size to %zu\n",
						DP_OID(oids[i]),  entry.chunk_size);
			}

			len = sprintf(oid_name, "%"PRIu64".%"PRIu64"", oids[i].hi, oids[i].lo);
			D_ASSERT(len <= DFS_MAX_NAME);
			rc = insert_entry(dfs->layout_v, now_dir->oh, DAOS_TX_NONE,
					  oid_name, len, DAOS_COND_DKEY_INSERT, &entry);
			if (rc) {
				D_ERROR("Failed to insert leaked entry in l+f (%d)\n", rc);
				D_GOTO(out_lf2, rc);
			}
			unmarked_entries++;
		}
	}

done:
	rc = clock_gettime(CLOCK_REALTIME, &current_time);
	if (rc)
		D_GOTO(out_lf2, rc = errno);
	D_PRINT("DFS checker: Done! (runtime: "DF_U64" sec)\n",
		current_time.tv_sec - oit_args->start_time);
	D_PRINT("DFS checker: Number of leaked OIDs in namespace = "DF_U64"\n", unmarked_entries);
	if (flags & DFS_CHECK_VERIFY) {
		if (oit_args->failed) {
			D_ERROR(""DF_U64" OIDs failed data consistency check!\n", oit_args->failed);
			D_GOTO(out_lf2, rc = EIO);
		}
	}
out_lf2:
	D_FREE(kds);
	D_FREE(dkey_enum_buf);
	D_FREE(entry_enum_buf);
	if (flags & DFS_CHECK_RELINK) {
		rc2 = dfs_release(now_dir);
		if (rc == 0)
			rc = rc2;
	}
out_lf1:
	if (flags & DFS_CHECK_RELINK) {
		rc2 = dfs_release(lf);
		if (rc == 0)
			rc = rc2;
	}
out_oit:
	rc2 = daos_oit_close(oit_args->oit, NULL);
	if (rc == 0)
		rc = daos_der2errno(rc2);
	D_FREE(oit_args);
out_snap:
	epr.epr_hi = epr.epr_lo = snap_epoch;
	rc2 = daos_cont_destroy_snap(coh, epr, NULL);
	if (rc == 0)
		rc = daos_der2errno(rc2);
out_dfs:
	rc2 = dfs_umount(dfs);
	if (rc == 0)
		rc = rc2;
out_cont:
	rc2 = daos_cont_close(coh, NULL);
	if (rc == 0)
		rc = daos_der2errno(rc2);

	return rc;
}

int
dfs_recreate_sb(daos_handle_t coh, dfs_attr_t *attr)
{
	uint32_t			props[] = {DAOS_PROP_CO_LAYOUT_TYPE, DAOS_PROP_CO_ROOTS};
	const int			num_props = ARRAY_SIZE(props);
	daos_prop_t			*prop;
	struct daos_prop_entry		*entry;
	struct daos_prop_co_roots	*roots;
	daos_handle_t			super_oh;
	struct dfs_entry		rentry = {0};
	struct timespec			now;
	int				i;
	int				rc, rc2;

	if (attr == NULL)
		return EINVAL;

	prop = daos_prop_alloc(num_props);
	if (prop == NULL)
		return ENOMEM;

	for (i = 0; i < num_props; i++)
		prop->dpp_entries[i].dpe_type = props[i];

	rc = daos_cont_query(coh, NULL, prop, NULL);
	if (rc) {
		D_ERROR("daos_cont_query() failed, "DF_RC"\n", DP_RC(rc));
		D_GOTO(out_prop, rc = daos_der2errno(rc));
	}

	entry = daos_prop_entry_get(prop, DAOS_PROP_CO_LAYOUT_TYPE);
	if (entry == NULL || entry->dpe_val != DAOS_PROP_CO_LAYOUT_POSIX) {
		D_ERROR("container is not of type POSIX\n");
		D_GOTO(out_prop, rc = EINVAL);
	}

	entry = daos_prop_entry_get(prop, DAOS_PROP_CO_ROOTS);
	D_ASSERT(entry != NULL);
	roots = (struct daos_prop_co_roots *)entry->dpe_val_ptr;
	if (daos_obj_id_is_nil(roots->cr_oids[0]) ||
	    daos_obj_id_is_nil(roots->cr_oids[1])) {
		D_ERROR("Invalid superblock or root object ID\n");
		D_GOTO(out_prop, rc = EIO);
	}

	/** Recreate SB */
	rc = open_sb(coh, true, true, DAOS_OO_RW, roots->cr_oids[0], attr, &super_oh, NULL);
	if (rc)
		D_GOTO(out_prop, rc);

	/** relink the root object */
	rentry.oid = roots->cr_oids[1];
	rentry.mode = S_IFDIR | 0755;
	rc = clock_gettime(CLOCK_REALTIME, &now);
	if (rc)
		D_GOTO(out_super, rc = errno);
	rentry.mtime = rentry.ctime = now.tv_sec;
	rentry.mtime_nano = rentry.ctime_nano = now.tv_nsec;
	rentry.uid = geteuid();
	rentry.gid = getegid();

	rc = insert_entry(DFS_LAYOUT_VERSION, super_oh, DAOS_TX_NONE, "/", 1, DAOS_COND_DKEY_INSERT,
			  &rentry);
	if (rc) {
		D_ERROR("Failed to insert root entry: %d (%s)\n", rc, strerror(rc));
		D_GOTO(out_super, rc);
	}

out_super:
	rc2 = daos_obj_close(super_oh, NULL);
	if (rc == 0)
		rc = daos_der2errno(rc2);
out_prop:
	daos_prop_free(prop);
	return rc;
}

int
dfs_relink_root(daos_handle_t coh)
{
	uint32_t			props[] = {DAOS_PROP_CO_LAYOUT_TYPE, DAOS_PROP_CO_ROOTS};
	const int			num_props = ARRAY_SIZE(props);
	daos_prop_t			*prop;
	struct daos_prop_entry		*entry;
	struct daos_prop_co_roots	*roots;
	daos_handle_t			super_oh;
	dfs_layout_ver_t		layout_v;
	dfs_attr_t			attr;
	bool				exists;
	struct dfs_entry		rentry = {0};
	struct timespec			now;
	int				i;
	int				rc, rc2;

	prop = daos_prop_alloc(num_props);
	if (prop == NULL)
		return ENOMEM;

	for (i = 0; i < num_props; i++)
		prop->dpp_entries[i].dpe_type = props[i];

	rc = daos_cont_query(coh, NULL, prop, NULL);
	if (rc) {
		D_ERROR("daos_cont_query() failed, "DF_RC"\n", DP_RC(rc));
		D_GOTO(out_prop, rc = daos_der2errno(rc));
	}

	entry = daos_prop_entry_get(prop, DAOS_PROP_CO_LAYOUT_TYPE);
	if (entry == NULL || entry->dpe_val != DAOS_PROP_CO_LAYOUT_POSIX) {
		D_ERROR("container is not of type POSIX\n");
		D_GOTO(out_prop, rc = EINVAL);
	}

	entry = daos_prop_entry_get(prop, DAOS_PROP_CO_ROOTS);
	D_ASSERT(entry != NULL);
	roots = (struct daos_prop_co_roots *)entry->dpe_val_ptr;
	if (daos_obj_id_is_nil(roots->cr_oids[0]) ||
	    daos_obj_id_is_nil(roots->cr_oids[1])) {
		D_ERROR("Invalid superblock or root object ID\n");
		D_GOTO(out_prop, rc = EIO);
	}

	/** Verify SB */
	rc = open_sb(coh, false, false, DAOS_OO_RW, roots->cr_oids[0], &attr, &super_oh, &layout_v);
	if (rc)
		D_GOTO(out_prop, rc);

	/** Check if super object has the root entry */
	rc = fetch_entry(layout_v, super_oh, DAOS_TX_NONE, "/", 1, false, &exists, &rentry,
			 0, NULL, NULL, NULL);
	if (rc) {
		D_ERROR("Failed to fetch object: %d (%s)\n", rc, strerror(rc));
		D_GOTO(out_super, rc);
	}
	if (exists) {
		D_PRINT("Root object already linked in SB\n");
		D_GOTO(out_super, rc = 0);
	}

	/** relink the root object */
	rentry.oid = roots->cr_oids[1];
	rentry.mode = S_IFDIR | 0755;
	rc = clock_gettime(CLOCK_REALTIME, &now);
	if (rc)
		D_GOTO(out_super, rc = errno);
	rentry.mtime = rentry.ctime = now.tv_sec;
	rentry.mtime_nano = rentry.ctime_nano = now.tv_nsec;
	rentry.uid = geteuid();
	rentry.gid = getegid();

	rc = insert_entry(layout_v, super_oh, DAOS_TX_NONE, "/", 1, DAOS_COND_DKEY_INSERT, &rentry);
	if (rc) {
		D_ERROR("Failed to insert root entry: %d (%s)\n", rc, strerror(rc));
		D_GOTO(out_super, rc);
	}

out_super:
	rc2 = daos_obj_close(super_oh, NULL);
	if (rc == 0)
		rc = daos_der2errno(rc2);
out_prop:
	daos_prop_free(prop);
	return rc;
}

int
dfs_obj_fix_type(dfs_t *dfs, dfs_obj_t *parent, const char *name)
{
	struct dfs_entry	entry = {0};
	bool			exists;
	daos_key_t		dkey;
	size_t			len;
	enum daos_otype_t	otype;
	mode_t			mode;
	d_sg_list_t		sgl;
	d_iov_t			sg_iov;
	daos_recx_t		recx;
	daos_iod_t		iod;
	int			rc;

	if (dfs == NULL || !dfs->mounted)
		return EINVAL;
	if (parent == NULL)
		parent = &dfs->root;
	else if (!S_ISDIR(parent->mode))
		return ENOTDIR;

	rc = check_name(name, &len);
	if (rc)
		return rc;

	rc = fetch_entry(dfs->layout_v, parent->oh, DAOS_TX_NONE, name, len, true, &exists, &entry,
			 0, NULL, NULL, NULL);
	if (rc) {
		D_ERROR("Failed to fetch entry %s (%d)\n", name, rc);
		D_GOTO(out, rc);
	}
	if (exists == false)
		D_GOTO(out, rc = ENOENT);

	/** get the object type from the oid */
	otype = daos_obj_id2type(entry.oid);

	/** reset the type bits to 0 and set 700 permission bits */
	mode = S_IWUSR | S_IRUSR | S_IXUSR;
	/** set the type bits according to oid type and entry value */
	if (daos_is_array_type(otype)) {
		mode |= S_IFREG;
		D_PRINT("Setting entry type to S_IFREG\n");
	} else if (entry.value_len) {
		mode |= S_IFLNK;
		D_PRINT("Setting entry type to S_IFLNK\n");
	} else {
		mode |= S_IFDIR;
		D_PRINT("Setting entry type to S_IFDIR\n");
	}

	/** Update mode bits on storage */
	d_iov_set(&dkey, (void *)name, len);
	d_iov_set(&iod.iod_name, INODE_AKEY_NAME, sizeof(INODE_AKEY_NAME) - 1);
	recx.rx_idx	= MODE_IDX;
	recx.rx_nr	= sizeof(mode_t);
	iod.iod_nr	= 1;
	iod.iod_recxs	= &recx;
	iod.iod_type	= DAOS_IOD_ARRAY;
	iod.iod_size	= 1;
	d_iov_set(&sg_iov, &mode, sizeof(mode_t));
	sgl.sg_nr	= 1;
	sgl.sg_nr_out	= 0;
	sgl.sg_iovs	= &sg_iov;
	rc = daos_obj_update(parent->oh, DAOS_TX_NONE, DAOS_COND_DKEY_UPDATE, &dkey, 1, &iod, &sgl,
			     NULL);
	if (rc) {
		D_ERROR("Failed to update object type "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc = daos_der2errno(rc));
	}

out:
	if (entry.value)
		D_FREE(entry.value);
	return rc;
}
