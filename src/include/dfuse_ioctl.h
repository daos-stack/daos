/**
 * (C) Copyright 2017-2020 Intel Corporation.
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
#ifndef __DFUSE_IOCTL_H__
#define __DFUSE_IOCTL_H__

#include <asm/ioctl.h>
#include "daos.h"

#define DFUSE_IOCTL_TYPE 0xA3       /* Arbitrary "unique" type of the IOCTL */
#define DFUSE_IOCTL_REPLY_BASE 0xC1 /* Number of the IOCTL.  Also arbitrary */
#define DFUSE_IOCTL_VERSION 6       /* Version of ioctl protocol */

#define DFUSE_IOCTL_REPLY_CORE (DFUSE_IOCTL_REPLY_BASE)

/* (DFUSE_IOCTL_REPLY_BASE + 1) is reserved by an older version of
 * IOCTL_REPLY_SIZE
 */

#define DFUSE_IOCTL_REPLY_POH (DFUSE_IOCTL_REPLY_BASE + 2)
#define DFUSE_IOCTL_REPLY_COH (DFUSE_IOCTL_REPLY_BASE + 3)
#define DFUSE_IOCTL_REPLY_DOH (DFUSE_IOCTL_REPLY_BASE + 4)
#define DFUSE_IOCTL_REPLY_DOOH (DFUSE_IOCTL_REPLY_BASE + 5)
#define DFUSE_IOCTL_REPLY_SIZE (DFUSE_IOCTL_REPLY_BASE + 6)
#define DFUSE_IOCTL_REPLY_DSIZE (DFUSE_IOCTL_REPLY_BASE + 7)

/* Core IOCTL reply */
struct dfuse_il_reply {
	int		fir_version;
	daos_obj_id_t	fir_oid;
	uuid_t		fir_pool;
	uuid_t		fir_cont;
};

/* Query for global pool/container handle sizes */
struct dfuse_hs_reply {
	int		fsr_version;
	size_t		fsr_pool_size;
	size_t		fsr_cont_size;
	size_t		fsr_dfs_size;
};

/* Query for global dfs/object handle sizes */
struct dfuse_hsd_reply {
	int		fsr_version;
	size_t		fsr_dobj_size;
};


/* Defines the IOCTL command to get the object ID for a open file */
#define DFUSE_IOCTL_IL ((int)_IOR(DFUSE_IOCTL_TYPE, DFUSE_IOCTL_REPLY_CORE, \
				  struct dfuse_il_reply))

/* Defined the IOCTL command to get the pool/container handle sizes for a
 * open file
 */
#define DFUSE_IOCTL_IL_SIZE ((int)_IOR(DFUSE_IOCTL_TYPE,		\
				       DFUSE_IOCTL_REPLY_SIZE,		\
				       struct dfuse_hs_reply))


/* Defined the IOCTL command to get the dfs/object handle sizes for a
 * open file
 */
#define DFUSE_IOCTL_IL_DSIZE ((int)_IOR(DFUSE_IOCTL_TYPE,		\
				       DFUSE_IOCTL_REPLY_DSIZE,		\
				       struct dfuse_hsd_reply))

#endif /* __DFUSE_IOCTL_H__ */
