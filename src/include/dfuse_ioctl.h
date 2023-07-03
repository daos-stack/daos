/**
 * (C) Copyright 2017-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#ifndef __DFUSE_IOCTL_H__
#define __DFUSE_IOCTL_H__

#include <asm/ioctl.h>
#include "daos.h"

#define DFUSE_IOCTL_TYPE         0xA3 /* Arbitrary "unique" type of the IOCTL */
#define DFUSE_IOCTL_REPLY_BASE   0xC1 /* Number of the IOCTL.  Also arbitrary */
#define DFUSE_IOCTL_VERSION      7    /* Version of ioctl protocol */

#define DFUSE_IOCTL_REPLY_CORE   (DFUSE_IOCTL_REPLY_BASE)

/* (DFUSE_IOCTL_REPLY_BASE + 1) is reserved by an older version of IOCTL_REPLY_SIZE */

#define DFUSE_IOCTL_REPLY_POH    (DFUSE_IOCTL_REPLY_BASE + 2)
#define DFUSE_IOCTL_REPLY_COH    (DFUSE_IOCTL_REPLY_BASE + 3)
#define DFUSE_IOCTL_REPLY_DOH    (DFUSE_IOCTL_REPLY_BASE + 4)
#define DFUSE_IOCTL_REPLY_DOOH   (DFUSE_IOCTL_REPLY_BASE + 5)
#define DFUSE_IOCTL_REPLY_SIZE   (DFUSE_IOCTL_REPLY_BASE + 6)
#define DFUSE_IOCTL_REPLY_DSIZE  (DFUSE_IOCTL_REPLY_BASE + 7)
#define DFUSE_IOCTL_REPLY_PFILE  (DFUSE_IOCTL_REPLY_BASE + 8)

#define DFUSE_IOCTL_R_DFUSE_USER (DFUSE_IOCTL_REPLY_BASE + 9)
#define DFUSE_COUNT_QUERY_CMD    (DFUSE_IOCTL_REPLY_BASE + 10)
#define DFUSE_IOCTL_EVICT_NR     (DFUSE_IOCTL_REPLY_BASE + 11)

/** Metadada caching is enabled for this file */
#define DFUSE_IOCTL_FLAGS_MCACHE (0x1)

/* Core IOCTL reply */
struct dfuse_il_reply {
	int           fir_version;
	daos_obj_id_t fir_oid;
	uuid_t        fir_pool;
	uuid_t        fir_cont;
	uint64_t      fir_flags;
};

/* Query for global pool/container handle sizes */
struct dfuse_hs_reply {
	int    fsr_version;
	size_t fsr_pool_size;
	size_t fsr_cont_size;
	size_t fsr_dfs_size;
};

/* Query for global dfs/object handle sizes */
struct dfuse_hsd_reply {
	int    fsr_version;
	size_t fsr_dobj_size;
};

struct dfuse_user_reply {
	uid_t uid;
	gid_t gid;
};

struct dfuse_mem_query {
	uint64_t inode_count;
	uint64_t fh_count;
	uint64_t pool_count;
	uint64_t container_count;
	ino_t    ino;
	bool     found;
};

/* Defines the IOCTL command to get the object ID for a open file */
#define DFUSE_IOCTL_IL ((int)_IOR(DFUSE_IOCTL_TYPE, DFUSE_IOCTL_REPLY_CORE, struct dfuse_il_reply))

/* Defines the IOCTL command to get the pool/container handle sizes for a open file */

#define DFUSE_IOCTL_IL_SIZE                                                                        \
	((int)_IOR(DFUSE_IOCTL_TYPE, DFUSE_IOCTL_REPLY_SIZE, struct dfuse_hs_reply))

/* Defines the IOCTL command to get the dfs/object handle sizes for a open file */
#define DFUSE_IOCTL_IL_DSIZE                                                                       \
	((int)_IOR(DFUSE_IOCTL_TYPE, DFUSE_IOCTL_REPLY_DSIZE, struct dfuse_hsd_reply))

/* Return the user running dfuse */
#define DFUSE_IOCTL_DFUSE_USER                                                                     \
	((int)_IOR(DFUSE_IOCTL_TYPE, DFUSE_IOCTL_R_DFUSE_USER, struct dfuse_user_reply))

#define DFUSE_IOCTL_COUNT_QUERY                                                                    \
	((int)_IOWR(DFUSE_IOCTL_TYPE, DFUSE_COUNT_QUERY_CMD, struct dfuse_mem_query))

#define DFUSE_IOCTL_DFUSE_EVICT                                                                    \
	((int)_IOR(DFUSE_IOCTL_TYPE, DFUSE_IOCTL_EVICT_NR, struct dfuse_mem_query))

#endif /* __DFUSE_IOCTL_H__ */
