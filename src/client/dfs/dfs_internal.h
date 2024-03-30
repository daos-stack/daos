/**
 * (C) Copyright 2019-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * Internal DFS client structs and functions
 */
#ifndef __DFS_INTERNAL_H__
#define __DFS_INTERNAL_H__

#include <fcntl.h>
#include <sys/stat.h>
#include <daos/common.h>
#include <daos.h>
#include <daos_fs.h>

/** D-key name of SB metadata */
#define SB_DKEY            "DFS_SB_METADATA"

#define SB_AKEYS           9
/** A-key name of SB magic */
#define MAGIC_NAME         "DFS_MAGIC"
/** A-key name of SB version */
#define SB_VER_NAME        "DFS_SB_VERSION"
/** A-key name of DFS Layout Version */
#define LAYOUT_VER_NAME    "DFS_LAYOUT_VERSION"
/** A-key name of Default chunk size */
#define CS_NAME            "DFS_CHUNK_SIZE"
/** A-key name of Default Object Class for objects */
#define OC_NAME            "DFS_OBJ_CLASS"
/** A-key name of Default Object Class for directories */
#define DIR_OC_NAME        "DFS_DIR_OBJ_CLASS"
/** A-key name of Default Object Class for files */
#define FILE_OC_NAME       "DFS_FILE_OBJ_CLASS"
/** Consistency mode of the DFS container */
#define CONT_MODE_NAME     "DFS_MODE"
/** A-key name of the object class hints */
#define CONT_HINT_NAME     "DFS_HINTS"

#define MAGIC_IDX          0
#define SB_VER_IDX         1
#define LAYOUT_VER_IDX     2
#define CS_IDX             3
#define OC_IDX             4
#define DIR_OC_IDX         5
#define FILE_OC_IDX        6
#define CONT_MODE_IDX      7
#define CONT_HINT_IDX      8

/** Magic Value */
#define DFS_SB_MAGIC       0xda05df50da05df50
/** DFS SB version value */
#define DFS_SB_VERSION     2
/** DFS Layout Version Value */
#define DFS_LAYOUT_VERSION 3
/** Magic value for serializing / deserializing a DFS handle */
#define DFS_GLOB_MAGIC     0xda05df50
/** Magic value for serializing / deserializing a DFS object handle */
#define DFS_OBJ_GLOB_MAGIC 0xdf500b90

/** Number of A-keys for attributes in any object entry */
#define INODE_AKEYS        12
#define INODE_AKEY_NAME    "DFS_INODE"
#define SLINK_AKEY_NAME    "DFS_SLINK"
#define MODE_IDX           0
#define OID_IDX            (sizeof(mode_t))
#define MTIME_IDX          (OID_IDX + sizeof(daos_obj_id_t))
#define CTIME_IDX          (MTIME_IDX + sizeof(uint64_t))
#define CSIZE_IDX          (CTIME_IDX + sizeof(uint64_t))
#define OCLASS_IDX         (CSIZE_IDX + sizeof(daos_size_t))
#define MTIME_NSEC_IDX     (OCLASS_IDX + sizeof(daos_oclass_id_t))
#define CTIME_NSEC_IDX     (MTIME_NSEC_IDX + sizeof(uint64_t))
#define UID_IDX            (CTIME_NSEC_IDX + sizeof(uint64_t))
#define GID_IDX            (UID_IDX + sizeof(uid_t))
#define SIZE_IDX           (GID_IDX + sizeof(gid_t))
#define HLC_IDX            (SIZE_IDX + sizeof(daos_size_t))
#define END_IDX            (HLC_IDX + sizeof(uint64_t))

/*
 * END IDX for layout V2 (2.0) is at the current offset where we store the mtime nsec, but also need
 * to account for the atime which is not stored anymore, but was stored (as a time_t) in layout V2.
 */
#define END_L2_IDX         (MTIME_NSEC_IDX + sizeof(time_t))

/** Parameters for dkey enumeration */
#define ENUM_DESC_NR       10
#define ENUM_DESC_BUF      (ENUM_DESC_NR * DFS_MAX_NAME)
#define ENUM_XDESC_BUF     (ENUM_DESC_NR * (DFS_MAX_XATTR_NAME + 2))

/** OIDs for Superblock and Root objects */
#define RESERVED_LO        0
#define SB_HI              0
#define ROOT_HI            1

/** DFS mode mask (3rd bit) */
#define MODE_MASK          (1 << 2)

/** Max recursion depth for symlinks */
#define DFS_MAX_RECURSION  40

/** MAX value for the HI OID */
#define MAX_OID_HI         ((1UL << 32) - 1)

typedef uint64_t dfs_magic_t;
typedef uint16_t dfs_sb_ver_t;
typedef uint16_t dfs_layout_ver_t;

/** object struct that is instantiated for a DFS open object */
struct dfs_obj {
	/** DAOS object ID */
	daos_obj_id_t oid;
	/** DAOS object open handle */
	daos_handle_t oh;
	/** mode_t containing permissions & type */
	mode_t        mode;
	/** open access flags */
	int           flags;
	/** DAOS object ID of the parent of the object */
	daos_obj_id_t parent_oid;
	/** entry name of the object in the parent */
	char          name[DFS_MAX_NAME + 1];
	union {
		/** Symlink value if object is a symbolic link */
		char *value;
		struct {
			/** Default object class for all entries in dir */
			daos_oclass_id_t oclass;
			/** Default chunk size for all entries in dir */
			daos_size_t      chunk_size;
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
	int                  mounted;
	/** flag to indicate whether dfs is mounted with balanced mode (DTX) */
	bool                 use_dtx;
	/** lock for threadsafety */
	pthread_mutex_t      lock;
	/** layout version of DFS container that is mounted */
	dfs_layout_ver_t     layout_v;
	/** uid - inherited from container. */
	uid_t                uid;
	/** gid - inherited from container. */
	gid_t                gid;
	/** Access mode (RDONLY, RDWR) */
	int                  amode;
	/** Open pool handle of the DFS mount */
	daos_handle_t        poh;
	/** refcount on pool handle that through the DFS API */
	uint32_t             poh_refcount;
	/** Open container handle of the DFS mount */
	daos_handle_t        coh;
	/** refcount on cont handle that through the DFS API */
	uint32_t             coh_refcount;
	/** Transaction handle epoch. DAOS_EPOCH_MAX for DAOS_TX_NONE */
	daos_epoch_t	     th_epoch;
	/** Transaction handle */
	daos_handle_t	     th;
	/** Object ID reserved for this DFS (see oid_gen below) */
	daos_obj_id_t        oid;
	/** superblock object OID */
	daos_obj_id_t        super_oid;
	/** Open object handle of SB */
	daos_handle_t        super_oh;
	/** Root object info */
	dfs_obj_t            root;
	/** DFS container attributes (Default chunk size, oclass, etc.) */
	dfs_attr_t           attr;
	/** Object class hint for files */
	daos_oclass_hints_t  file_oclass_hint;
	/** Object class hint for dirs */
	daos_oclass_hints_t  dir_oclass_hint;
	/** Optional prefix to account for when resolving an absolute path */
	char                *prefix;
	daos_size_t          prefix_len;
	/** hash entry for pool open handle - valid on dfs_connect() */
	struct dfs_mnt_hdls *pool_hdl;
	/** hash entry for cont open handle - valid on dfs_connect() */
	struct dfs_mnt_hdls *cont_hdl;
	/** the root dir stat buf */
	struct stat          root_stbuf;
};

struct dfs_entry {
	/** mode (permissions + entry type) */
	mode_t           mode;
	/* Length of value string, not including NULL byte */
	daos_size_t      value_len;
	/** Object ID if not a symbolic link */
	daos_obj_id_t    oid;
	/* Time of last modification (sec) */
	uint64_t         mtime;
	/* Time of last modification (nsec) */
	uint64_t         mtime_nano;
	/* for regular file, the time of last modification of the object */
	uint64_t         obj_hlc;
	/* Time of last status change (sec) */
	uint64_t         ctime;
	/* Time of last status change (nsec) */
	uint64_t         ctime_nano;
	/** chunk size of file or default for all files in a dir */
	daos_size_t      chunk_size;
	/** oclass of file or all files in a dir */
	daos_oclass_id_t oclass;
	/** uid - not enforced at this level. */
	uid_t            uid;
	/** gid - not enforced at this level. */
	gid_t            gid;
	/** Sym Link value */
	char            *value;
};

/** enum for hash entry type */
enum {
	DFS_H_POOL,
	DFS_H_CONT,
};

/** hash entry for open pool/container handles */
struct dfs_mnt_hdls {
	d_list_t      entry;
	char          value[DAOS_PROP_LABEL_MAX_LEN * 2 + 1];
	daos_handle_t handle;
	int           ref;
	int           type;
};

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
	else if ((flags & O_ACCMODE) == O_RDWR || (flags & O_ACCMODE) == O_WRONLY)
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
		ret = daos_tx_close(th, NULL);
		if (ret) {
			D_ERROR("daos_tx_close() failed (%d)\n", ret);
			if (rc == 0)
				rc = daos_der2errno(ret);
		}
	}

	return rc;
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

static inline char *
concat(const char *s1, const char *s2)
{
	char *result = NULL;

	D_ASPRINTF(result, "%s%s", s1, s2);
	if (result == NULL)
		return NULL;

	return result;
}

/*
 * OID generation for the dfs objects.
 *
 * The oid.lo uint64_t value will be allocated from the DAOS container using the
 * unique oid allocator. 1 oid at a time will be allocated for the dfs mount.
 * The oid.hi value has the high 32 bits reserved for DAOS (obj class, type,
 * etc.). The lower 32 bits will be used locally by the dfs mount point, and
 * hence discarded when the dfs is unmounted.
 */
static inline int
oid_gen(dfs_t *dfs, daos_oclass_id_t oclass, bool file, daos_obj_id_t *oid)
{
	enum daos_otype_t type = DAOS_OT_MULTI_HASHED;
	int               rc;

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
		D_ERROR("daos_obj_generate_oid() failed " DF_RC "\n", DP_RC(rc));
		return daos_der2errno(rc);
	}

	return 0;
}

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

int
get_oclass_hints(const char *hints, daos_oclass_hints_t *dir_hints, daos_oclass_hints_t *file_hints,
		 uint64_t rf);
int
open_sb(daos_handle_t coh, bool create, bool punch, int omode, daos_obj_id_t super_oid,
	dfs_attr_t *attr, daos_handle_t *oh, dfs_layout_ver_t *ver);
int
insert_entry(dfs_layout_ver_t ver, daos_handle_t oh, daos_handle_t th, const char *name, size_t len,
	     uint64_t flags, struct dfs_entry *entry);
int
fetch_entry(dfs_layout_ver_t ver, daos_handle_t oh, daos_handle_t th, const char *name, size_t len,
	    bool fetch_sym, bool *exists, struct dfs_entry *entry, int xnr, char *xnames[],
	    void *xvals[], daos_size_t *xsizes);
int
remove_entry(dfs_t *dfs, daos_handle_t th, daos_handle_t parent_oh, const char *name, size_t len,
	     struct dfs_entry entry);
int
open_dir(dfs_t *dfs, dfs_obj_t *parent, int flags, daos_oclass_id_t cid, struct dfs_entry *entry,
	 size_t len, dfs_obj_t *dir);
int
create_dir(dfs_t *dfs, dfs_obj_t *parent, daos_oclass_id_t cid, dfs_obj_t *dir);
int
entry_stat(dfs_t *dfs, daos_handle_t th, daos_handle_t oh, const char *name, size_t len,
	   struct dfs_obj *obj, bool get_size, struct stat *stbuf, uint64_t *obj_hlc);
int
get_num_entries(daos_handle_t oh, daos_handle_t th, uint32_t *nr, bool check_empty);
int
update_stbuf_times(struct dfs_entry entry, daos_epoch_t max_epoch, struct stat *stbuf,
		   uint64_t *obj_hlc);
int
lookup_rel_path(dfs_t *dfs, dfs_obj_t *root, const char *path, int flags, dfs_obj_t **_obj,
		mode_t *mode, struct stat *stbuf, size_t depth);
#endif /* __DFS_INTERNAL_H__ */
