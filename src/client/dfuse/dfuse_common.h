/**
 * (C) Copyright 2016-2019 Intel Corporation.
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

#ifndef __DFUSE_COMMON_H__
#define __DFUSE_COMMON_H__

#ifndef D_LOGFAC
#define D_LOGFAC DD_FAC(dfuse)
#endif

#include "dfuse_log.h"

#include <sys/stat.h>
#include <gurt/common.h>

#include "dfuse_gah.h"

struct dfuse_string_out {
	d_string_t path;
	int rc;
	int err;
};

struct dfuse_entry_out {
	struct ios_gah gah;
	struct stat stat;
	int rc;
	int err;
};

struct dfuse_attr_out {
	struct stat stat;
	int rc;
	int err;
};

struct dfuse_open_out {
	struct ios_gah gah;
	int rc;
	int err;
};

struct dfuse_data_out {
	d_iov_t data;
	int rc;
	int err;
};

struct dfuse_status_out {
	int rc;
	int err;
};

#endif /* __DFUSE_COMMON_H__ */
