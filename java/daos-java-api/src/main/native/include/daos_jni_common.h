#include <stdio.h>
#include <daos.h>
#include <daos_fs.h>

#ifndef _INCLUDED_DAOS_JNI_COMMON
#define _INCLUDED_DAOS_JNI_COMMON

/** OIDs for Superblock and Root objects */
#define RESERVED_LO	0
#define SB_HI		0
#define ROOT_HI		1

struct dfs_entry {
	/** mode (permissions + entry type) */
	mode_t		mode;
	/** Object ID if not a symbolic link */
	daos_obj_id_t	oid;
	/** chunk size of file */
	daos_size_t	chunk_size;
	/** Sym Link value */
	char		*value;
	/* Time of last access */
	time_t		atime;
	/* Time of last modification */
	time_t		mtime;
	/* Time of last status change */
	time_t		ctime;
};

#endif
