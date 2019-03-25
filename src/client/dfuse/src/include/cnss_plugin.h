/* Copyright (C) 2016-2018 Intel Corporation
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
/* Users of this plugin interface should copy the file from
 * <iof_prefix>/share/plugin to their source tree so to ensure
 * a forward compatible plugin
 */
#include <unistd.h>
#include <stdbool.h>
#include <inttypes.h>

#ifndef __CNSS_PLUGIN_H__
#define __CNSS_PLUGIN_H__

#if defined(__cplusplus)
extern "C" {
#endif

#define CNSS_SUCCESS           0
#define CNSS_ERR_PREFIX        1 /*CNSS prefix is not set in the environment*/
#define CNSS_ERR_NOMEM         2 /*no memory*/
#define CNSS_ERR_PLUGIN        3 /*failed to load or initialize plugin*/
#define CNSS_ERR_CART          4 /*CaRT failed*/

struct fuse_lowlevel_ops;
struct fuse_args;
struct fuse_session;

/* Function lookup table provided by CNSS to plugin */
struct cnss_plugin_cb {
	void *handle;
};

bool
cnss_register_fuse(void *arg,
		   struct fuse_lowlevel_ops *flo,
		   struct fuse_args *args,
		   const char *mnt,
		   bool threaded,
		   void *private_data,
		   struct fuse_session **sessionp);

void
iof_reg(void *arg, struct cnss_plugin_cb *cb);

void
iof_post_start(void *arg);

void
iof_finish(void *arg);

void
iof_flush_fuse(void *arg);

int
iof_deregister_fuse(void *arg);

/* Function lookup table provided by plugin to CNSS. */
struct cnss_plugin {
	void *handle;  /** Handle passed back to all callback functions */
};

#if defined(__cplusplus)
}
#endif

#endif /* __CNSS_PLUGIN_H__ */
