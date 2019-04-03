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

#ifndef __DFUSE_CNSS_H__
#define __DFUSE_CNSS_H__

#include "dfuse_log.h"

/**
 * These pre-date -DER_* codes, and are used for exit status on failure so
 * keep them for now until we can test a replacement.
 */

#define CNSS_SUCCESS           0
#define CNSS_ERR_PREFIX        1 /*CNSS prefix is not set in the environment*/
#define CNSS_ERR_NOMEM         2 /*no memory*/
#define CNSS_ERR_PLUGIN        3 /*failed to load or initialize plugin*/
#define CNSS_ERR_CART          4 /*CaRT failed*/

#include "dfuse_common.h"
#include "dfuse.h"

struct fs_info {
	char			*fsi_mnt;
	struct fuse		*fsi_fuse;
	struct fuse_session	*fsi_session;
	pthread_t		fsi_thread;
	pthread_mutex_t		fsi_lock;
	struct dfuse_projection_info *fsi_handle;
	bool			fsi_running;
	bool			fsi_mt;
};

struct cnss_info {
	struct dfuse_state	*dfuse_state;
	struct fs_info		ci_fsinfo;
};

bool
cnss_register_fuse(struct cnss_info *cnss_info,
		   struct fuse_lowlevel_ops *flo,
		   struct fuse_args *args,
		   const char *mnt,
		   bool threaded,
		   void *private_data,
		   struct fuse_session **sessionp);

struct dfuse_state *
dfuse_plugin_init();

void
dfuse_reg(struct dfuse_state *dfuse_state, struct cnss_info *cnss_info);

void
dfuse_post_start(struct dfuse_state *dfuse_state);

void
dfuse_finish(struct dfuse_state *dfuse_state);

void
dfuse_flush_fuse(struct dfuse_projection_info *fs_handle);

int
dfuse_deregister_fuse(struct dfuse_projection_info *fs_handle);

#endif /* __DFUSE_CNSS_H__ */
