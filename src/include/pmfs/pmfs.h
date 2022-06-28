/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __PMFS_H__
#define __PMFS_H__

#include <dirent.h>
#include <daos_types.h>


/** Maximum Name length */
#define PMFS_MAX_NAME		NAME_MAX
/** Maximum PATH length */
#define PMFS_MAX_PATH		PATH_MAX

#define PMFS_BALANCED	4 /** PMFS operations using a DTX */
#define PMFS_RELAXED	0 /** PMFS operations do not use a DTX (default mode). */
#define PMFS_RDONLY	O_RDONLY
#define PMFS_RDWR	O_RDWR

/** Maximum tasks */
#define PMFS_MAX_TASKS	128

/** D-key name of SB metadata */
#define SB_DKEY		"PMFS_SB_METADATA"

#define SB_AKEYS	6
/** A-key name of SB magic */
#define MAGIC_NAME	"PMFS_MAGIC"
/** A-key name of SB version */
#define SB_VERSION_NAME	"PMFS_SB_VERSION"
/** A-key name of DFS Layout Version */
#define LAYOUT_NAME	"PMFS_LAYOUT_VERSION"
/** A-key name of Default chunk size */
#define CS_NAME		"PMFS_CHUNK_SIZE"
/** Consistency mode of the DFS container. */
#define MODE_NAME	"PMFS_MODE"
/** Allocated maximum OID value */
#define OID_VALUE	"PMFS_OID_VALUE"

/** Magic Value */
#define PMFS_SB_MAGIC	0xda05df50da05df50
/** PMFS SB version value */
#define PMFS_SB_VERSION	2
/** PMFS Layout Version Value */
#define PMFS_LAYOUT_VERSION	2
/** Array object stripe size for regular files */
#define PMFS_DEFAULT_CHUNK_SIZE	1048576

/** Number of A-keys for attributes in any object entry */
#define INODE_AKEYS	8
#define INODE_AKEY_NAME	"PMFS_INODE"
#define MODE_IDX	0
#define OID_IDX		(sizeof(mode_t))
#define ATIME_IDX	(OID_IDX + sizeof(daos_obj_id_t))
#define MTIME_IDX	(ATIME_IDX + sizeof(time_t))
#define CTIME_IDX	(MTIME_IDX + sizeof(time_t))
#define CSIZE_IDX	(CTIME_IDX + sizeof(time_t))
#define FSIZE_IDX	(CSIZE_IDX + sizeof(daos_size_t))
#define SYML_IDX	(FSIZE_IDX + sizeof(daos_size_t))

/** OIDs for Superblock and Root objects */
#define RESERVED_LO	0
#define SB_HI		0
#define ROOT_HI		1

/** Max recursion depth for symlinks */
#define PMFS_MAX_RECURSION	40

/** struct holding attributes for a PMFS container */
struct  pmfs_attr {
	/** Optional user ID for PMFS container. */
	uint64_t da_id;
	/** Default Chunk size for all files in container */
	daos_size_t da_chunk_size;
	/** Default Object Class for all objects in the container */
	uint32_t da_oclass_id;
	/*
	 * Consistency mode for the PMFS container: PMFS_RELAXED, PMFS_BALANCED.
	 * If set to 0 or more generally not set to balanced explicitly, relaxed
	 * mode will be used. In the future, Balanced mode will be the default.
	 */
	uint32_t da_mode;
};

/** object struct that is instantiated for a PMFS open object */
struct pmfs_obj {
	/* Reference number */
	int			ref;
	/** DAOS object ID */
	daos_obj_id_t		oid;
	/** mode_t containing permissions & type */
	mode_t			mode;
	/** open access flags */
	int			flags;
	/** DAOS object ID of the parent of the object */
	daos_obj_id_t		parent_oid;
	/** entry name of the object in the parent */
	char			name[PMFS_MAX_NAME + 1];
	/** File size */
	daos_size_t		file_size;
	/** Symlink value if object is a symbolic link */
	char			*value;
	/** Default chunk size for all entries in dir */
	daos_size_t		chunk_size;
};

/** pmfs struct that is instantiated for a mounted PMFS namespace */
struct pmfs {
	/** flag to indicate whether the pmfs is mounted */
	bool			mounted;
	/** flag to indicate whether pmfs is mounted with balanced mode (DTX) */
	bool			use_dtx;
	/** lock for threadsafety */
	pthread_mutex_t		lock;
	/** uid - inherited from container. */
	uid_t			uid;
	/** gid - inherited from container. */
	gid_t			gid;
	/** Access mode (RDONLY, RDWR) */
	int			amode;
	/** Open pool handle of the PMFS */
	daos_handle_t		poh;
	/** Open container handle of the PMFS */
	daos_handle_t		coh;
	/** Object ID reserved for this PMFS (see oid_gen below) */
	daos_obj_id_t		oid;
	/** superblock object OID */
	daos_obj_id_t		super_oid;
	/** Root object info */
	struct pmfs_obj		root;
	/** PMFS container attributes (Default chunk size, etc.) */
	struct pmfs_attr	attr;
	/** Task ring list */
	struct spdk_ring	*task_ring;
};

struct pmfs_entry {
	/** mode (permissions + entry type) */
	mode_t			mode;
	/* Length of value string, not including NULL byte */
	uint16_t		value_len;
	/** Object ID if not a symbolic link */
	daos_obj_id_t		oid;
	/* Time of last access */
	time_t			atime;
	/* Time of last modification */
	time_t			mtime;
	/* Time of last status change */
	time_t			ctime;
	/** chunk size of file or default for all files in a dir */
	daos_size_t		chunk_size;
	/** size of regular file */
	daos_size_t		file_size;
	/** Sym Link value */
	char			*value;
};

/** Raw level APIs for persistent memory file system commands */
/**
 * mkfs: format pool with persistent memory file system.
 *
 * \param poh Pool handler.
 * \param uuid Argument container uuid.
 * \return 0 on success, negative errno on failure.
 */
int pmfs_mkfs(daos_handle_t poh, uuid_t uuid);

/**
 * mount: mount to formatted persistent memory file system.
 *
 * \param poh Pool handler.
 * \param coh Container handler.
 * \param flags Access type (e.g. O_RDWR).
 * \param pmfs Output pointer of pmfs after mounted.
 * \return 0 on success, negative errno on failure.
 */
int pmfs_mount(daos_handle_t poh, daos_handle_t coh, int flags, struct pmfs **pmfs);
/**
 * umount: umount persistent memory file system.
 *
 * \param pmfs Pointer of valid file system.
 * \return 0 on success, negative errno on failure.
 */
int pmfs_umount(struct pmfs *pmfs);
/**
 * mkdir: create a dir on the persistent memory file system.
 *
 * \param pmfs Pointer of file system.
 * \param parent The parent opening object that want to make a dir in.
 * \param name Directory name that we want to make.
 * \return 0 on success, negative errno on failure.
 */
int pmfs_mkdir(struct pmfs *pmfs, struct pmfs_obj *parent, const char *name,
	       mode_t mode);
/**
 * listdir: list the children numbers of a dir.
 *
 * \param pmfs Pointer of persistent memory file system.
 * \param obj The opening object of the current dir.
 * \param nr  An output number of the children for the current dir.
 * \return 0 on success, negative errno on failure.
 */
int pmfs_listdir(struct pmfs *pmfs, struct pmfs_obj *obj, uint32_t *nr);
/**
 * remove: delete a file or a dir with its children..
 *
 * \param pmfs Pointer of the file system.
 * \param parent The parent opening object.
 * \param name The name of the file or dir that want to delete.
 * \param force to remove the dir even it has children.
 * \param oid An output value and return the entry object ID.
 * \return 0 on success, negative errno on failure.
 */
int pmfs_remove(struct pmfs *pmfs, struct pmfs_obj *parent, const char *name, bool force,
		daos_obj_id_t *oid);
/**
 * open: create/open a dir or file according to flag to create or open.
 *
 * \param pmfs Pointer of the file system.
 * \param parent The parent object of the opening object.
 * \param name The name or dir that want to open.
 * \param mode The opening mode of the file or dir(e.g.: S_IFREG/S_IFDIR/S_IFLNK).
 * \param flag The flag of a dir of file for opening(e.g.: O_CREAT).
 * \param chunk_size When creating a file with the chunk_size.
 * \param value This is reserved for S_IFLNK mode path.
 * \param _obj An output object pointer for the opening file or dir.
 * \return 0 on success, negative errno on failure.
 */
int pmfs_open(struct pmfs *pmfs, struct pmfs_obj *parent, const char *name, mode_t mode,
	      int flags, daos_size_t chunk_size, const char *value,
	      struct pmfs_obj **_obj);
/**
 * readdir: foreach the dir and put them in the dirent structure.
 *
 * \param pmfs Pointer of the file system.
 * \param obj The opening object of a dir that want to read.
 * \param nr The number of the children for a dir.
 * \param dirent The common struct dirent for linux, that after finishing readding dir
 *	  we can get its children.
 * \return 0 on success, negative errno on failure.
 */
int pmfs_readdir(struct pmfs *pmfs, struct pmfs_obj *obj, uint32_t *nr, struct dirent *dirs);
/**
 * release: Close the opening object and free its resources.
 *
 * \param obj Pointer of the opening object.
 * \return 0 on success, negative errno on failure.
 */
int pmfs_release(struct pmfs_obj *obj);
/**
 * lookup: lookup the given path and get the stat.
 *
 * \param pmfs Pointer of the file system.
 * \param path The absolute path that want to lookup.
 * \param flags The flag for indicating O_ACCMODE.
 * \param _obj  An output value for getting the found object pointer.
 * \param mode  mode_t containing permissions & type.
 * \param stbuf The detailed information about a file or dir.
 * \return 0 on success, negative errno on failure.
 */
int pmfs_lookup(struct pmfs *pmfs, const char *path, int flags, struct pmfs_obj **_obj,
		mode_t *mode, struct stat *stbuf);
/**
 * punch: punch the contents of file.
 *
 * \param pmfs Pointer of file system.
 * \param obj The opening object that want to punch for a file.
 * \param offset The offset means start byte address to punch a file.
 * \param len The len means from start address (offset) to punch a file with length len.
 * \return 0 on success, negative errno on failure.
 */
int pmfs_punch(struct pmfs *pmfs, struct pmfs_obj *obj, daos_off_t offset, daos_size_t len);
/**
 * write_sync: write a file with user_sgl and offset.
 *
 * \param pmfs Pointer of the file system.
 * \param obj  Opening object for the file.
 * \param user_sgl  Input argument with filling contents for writing to backend device.
 * \param off  The offset for a file to start writing.
 * \return 0 on success, negative errno on failure.
 */
int pmfs_write_sync(struct pmfs *pmfs, struct pmfs_obj *obj, d_sg_list_t *usr_sgl, daos_off_t off);
/**
 * read_sync: read the contents of a existed file.
 *
 * \param pmfs Pointer of the file system.
 * \param obj Opening object for the file.
 * \param user_sgl Input argument for calculating total iov_len and
 *		   output argument for data mapping for reading.
 * \param off  Start byte address for reading the contents of a file.
 * \param read_size  Input argument for the size user wanted to read.
 *		     Output argument for the real size the user read.
 * \return 0 on success, negative errno on failure.
 */
int pmfs_read_sync(struct pmfs *pmfs, struct pmfs_obj *obj, d_sg_list_t *usr_sgl, daos_off_t off,
		   daos_size_t *read_size);
/**
 * stat: show the stat of dir or a file.
 *
 * \param pmfs Pointer of the file system.
 * \param parent The parent object of the opening object.
 * \param name The name of the file or dir that want to get stat.
 * \param stat Output argument for the file or dir that got the stat.
 * \return 0 on success, negative errno on failure.
 */
int pmfs_stat(struct pmfs *pmfs, struct pmfs_obj *parent, const char *name, struct stat *stbuf);
/**
 * truncate: using for truncate a file to user defined size.
 *
 * \param pmfs Pointer of the file system.
 * \param obj The opening object of the file that want to truncate.
 * \param obj The size(aka length) of the file that want to truncate.
 * \return 0 on success, negative errno on failure.
 */
int pmfs_truncate(struct pmfs *pmfs, struct pmfs_obj *obj, daos_size_t len);
/**
 * rename: rename a file or dir.
 *
 * \param pmfs Pointer of the file system.
 * \param parent The parent object of the opening object that want to rename.
 * \param old_name The old name of a file or dir that want to rename.
 * \param new_name The new name of a file or dir that want to be.
 * \return 0 on success, negative errno on failure.
 */
int pmfs_rename(struct pmfs *pmfs, struct pmfs_obj *parent, const char *old_name,
		const char *new_name);
/**
 * get_file_size: get the size of a file.
 *
 * \param pmfs Pointer of the file system.
 * \param obj The parent object of the opening object that want to get file size.
 * \param fsize Output argument after getting the file size of a file or dir.
 * \return 0 on success, negative errno on failure.
 */
int pmfs_obj_get_file_size(struct pmfs *pmfs, struct pmfs_obj *obj, daos_size_t *fsize);
#endif /* __PMFS_H__ */
