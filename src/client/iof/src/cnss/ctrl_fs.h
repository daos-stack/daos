/* Copyright (C) 2016-2017 Intel Corporation
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
#ifndef __CTRL_FS_H__
#define __CTRL_FS_H__

#if defined(__cplusplus)
extern "C" {
#endif

#include "cnss_plugin.h"

struct ctrl_dir;

/* Starts the control file system, mounted at <prefix> */
int ctrl_fs_start(const char *prefix);
/* Signals to disable new opens on control file system */
int ctrl_fs_disable(void);
/* Stops control file system and blocks until it exits */
int ctrl_fs_shutdown(void);

/* Create a control subdirectory.  If one exists already of the same
 * name, it returns a pointer to the existing node.
 * \param[in] dir Parent directory (NULL means root)
 * \param[in] name Name of the subdirectory
 * \param[out] newdir A pointer to the new node
 */
int ctrl_create_subdir(struct ctrl_dir *dir, const char *name,
		       struct ctrl_dir **newdir);

/* Register a control variable.
 * \param[in] dir Parent directory (NULL means root)
 * \param[in] name Name of the subdirectory
 * \param[in] read_cb Optional callback to populate the value on read
 * \param[in] write_cb Optional callback to consume the value on write
 * \param[in] destroy_cb Optional callback to free associated data on exit
 * \param[in] cb_arg Optional argument to pass to callbacks
 */
int ctrl_register_variable(struct ctrl_dir *dir, const char *name,
			   ctrl_fs_read_cb_t read_cb,
			   ctrl_fs_write_cb_t write_cb,
			   ctrl_fs_destroy_cb_t destroy_cb, void *cb_arg);
/* Register a control event.
 * \param[in] dir Parent directory (NULL means root)
 * \param[in] name Name of the subdirectory
 * \param[in] trigger_cb Optional callback to invoke when the file is touched
 * \param[in] destroy_cb Optional callback to free associated data on exit
 * \param[in] cb_arg Optional argument to pass to callbacks
 */
int ctrl_register_event(struct ctrl_dir *dir, const char *name,
			ctrl_fs_trigger_cb_t trigger_cb,
			ctrl_fs_destroy_cb_t destroy_cb, void *cb_arg);
/* Register a control constant.
 * \param[in] dir Parent directory (NULL means root)
 * \param[in] name Name of the subdirectory
 * \param[in] value Contents of the file
 */
int ctrl_register_constant(struct ctrl_dir *dir, const char *name,
			   const char *value);

/* Register a 64-bit integer control constant.
 * \param[in] dir Parent directory (NULL means root)
 * \param[in] name Name of the subdirectory
 * \param[in] value Contents of the file
 */
int ctrl_register_constant_int64(struct ctrl_dir *dir, const char *name,
				 int64_t value);

/* Register a 64-bit unsigned integer control constant.
 * \param[in] dir Parent directory (NULL means root)
 * \param[in] name Name of the subdirectory
 * \param[in] value Contents of the file
 */
int ctrl_register_constant_uint64(struct ctrl_dir *dir, const char *name,
				  uint64_t value);

/* Register a control tracker
 * \param[in] dir Parent directory (NULL means root)
 * \param[in] name Name of the subdirectory
 * \param[in] open_cb Optional callback to invoke when the file opened
 * \param[in] close_cb Optional callback to invoke when the file closed
 * \param[in] destroy_cb Optional callback to free associated data on exit
 * \param[in] cb_arg Optional argument to pass to callbacks
 */
int ctrl_register_tracker(struct ctrl_dir *dir, const char *name,
			  ctrl_fs_open_cb_t open_cb,
			  ctrl_fs_close_cb_t close_cb,
			  ctrl_fs_destroy_cb_t destroy_cb, void *cb_arg);

int ctrl_register_uint64_variable(struct ctrl_dir *dir,
				  const char *name,
				  ctrl_fs_uint64_read_cb_t read_cb,
				  ctrl_fs_uint64_write_cb_t write_cb,
				  void *cb_arg);

#if defined(__cplusplus)
extern "C" }
#endif

#endif /* __CTRL_FS_H__ */
