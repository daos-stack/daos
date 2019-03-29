/* Copyright (C) 2017 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted for any purpose (including commercial purposes)
 * provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions, and the following disclaimer in the
 *    documentation and/or materials provided with the distribution.
 *
 * 3. In addition, redistributions of modified forms of the source or binary
 *    code must carry prominent notices stating that the original code was
 *    changed and the date of the change.
 *
 *  4. All publications or advertising materials mentioning features or use of
 *     this software are asked, but not required, to acknowledge that it was
 *     developed by Intel Corporation and credit the contributors.
 *
 * 5. Neither the name of Intel Corporation, nor the name of any Contributor
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef __CNSS_H__
#define __CNSS_H__

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
	struct iof_projection_info *fsi_handle;
	bool			fsi_running;
	bool			fsi_mt;
};

struct cnss_info {
	struct iof_state	*iof_state;
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

struct iof_state *
iof_plugin_init();

void
iof_reg(struct iof_state *iof_state, struct cnss_info *cnss_info);

void
iof_post_start(struct iof_state *iof_state);

void
iof_finish(struct iof_state *iof_state);

void
iof_flush_fuse(struct iof_projection_info *fs_handle);

int
iof_deregister_fuse(struct iof_projection_info *fs_handle);

#endif /* __CNSS_H__ */
