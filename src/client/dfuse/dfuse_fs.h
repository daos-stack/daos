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

#ifndef __DFUSE_FS_H__
#define __DFUSE_FS_H__

#include <stdbool.h>
#include <sched.h>
#include "dfuse_gah.h"
#include <gurt/atomic.h>

struct dfuse_service_group {
	bool			enabled;   /* Indicates group is available */
};

/** Projection specific information held on the client.
 *
 * Shared between CNSS and IL.
 */
struct dfuse_projection {
	/** Server group info */
	struct dfuse_service_group	*grp;
	/** bulk threshold */
	uint32_t			max_iov_write;
	/** max write size */
	uint32_t			max_write;
	/** client projection id */
	int				cli_fs_id;
	/** Projection enabled flag */
	bool				enabled;
	/** True if there is a progress thread configured */
	bool				progress_thread;
};

/* Common data stored on open file handles */
struct dfuse_file_common {
	daos_handle_t		oh;
	struct dfuse_projection	*projection;
	struct ios_gah		gah;
};

#endif /* __DFUSE_FS_H__ */
