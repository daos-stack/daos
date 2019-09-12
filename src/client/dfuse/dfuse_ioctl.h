/**
 * (C) Copyright 2017-2019 Intel Corporation.
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
#define DFUSE_IOCTL_REPLY_NUMBER 0xC1 /* Number of the IOCTL.  Also arbitrary */
#define DFUSE_IOCTL_VERSION 4       /* Version of ioctl protocol */

struct dfuse_il_reply {
	int		fir_version;
	daos_obj_id_t	fir_oid;
	uuid_t		fir_pool;
	uuid_t		fir_cont;
};

/* Defines the IOCTL command to get the object ID for a open file */
#define DFUSE_IOCTL_IL ((int)_IOR(DFUSE_IOCTL_TYPE, DFUSE_IOCTL_REPLY_NUMBER, \
				 struct dfuse_il_reply))

#endif /* __DFUSE_IOCTL_H__ */
