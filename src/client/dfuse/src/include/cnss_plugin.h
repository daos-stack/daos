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

#define CNSS_SUCCESS		0
#define CNSS_ERR_PREFIX		1 /*CNSS prefix is not set in the environment*/
#define CNSS_ERR_NOMEM		2 /*no memory*/
#define CNSS_ERR_PLUGIN		3 /*failed to load or initialize plugin*/
#define CNSS_ERR_FUSE		4 /*failed to register or deregister FUSE*/
#define CNSS_ERR_CART		5 /*CaRT failed*/
#define CNSS_BAD_DATA		6 /*bad data*/
#define CNSS_ERR_CTRL_FS	7 /*ctrl fs did not start or shutdown*/
#define CNSS_ERR_PTHREAD	8 /*failed to create or destroy CNSS threads*/

struct fuse_operations;
struct fuse_lowlevel_ops;
struct fuse_args;
struct fuse_session;
struct ctrl_dir;

/* Optional callback invoked when a read is done on a ctrl fs variable */
typedef int (*ctrl_fs_read_cb_t)(char *buf, size_t buflen, void *cb_arg);
/* Optional callback invoked when a write is done on a ctrl fs variable */
typedef int (*ctrl_fs_write_cb_t)(const char *value, void *cb_arg);

/* Optional callback invoked when an open to retrieve a value for a ctrl
 * fs tracker which value will be passed to the close callback
 */
typedef int (*ctrl_fs_open_cb_t)(int *value, void *cb_arg);
/* Optional callback invoked when a close is done on a ctrl fs tracker */
typedef int (*ctrl_fs_close_cb_t)(int value, void *cb_arg);
/* Optional callback invoked when ctrl fs is shutting down */
typedef int (*ctrl_fs_destroy_cb_t)(void *cb_arg);
/* Optional callback invoked when a trigger is done on a ctrl fs event.
 * A trigger occurs on any modification to the underlying file.
 */
typedef int (*ctrl_fs_trigger_cb_t)(void *cb_arg);

typedef uint64_t (*ctrl_fs_uint64_read_cb_t)(void *cb_arg);
typedef int (*ctrl_fs_uint64_write_cb_t)(uint64_t value, void *cb_arg);

/* Function lookup table provided by CNSS to plugin */
struct cnss_plugin_cb {
	void *handle;
	struct ctrl_dir *plugin_dir;
	const char *prefix;
	int fuse_version;
	const char *(*get_config_option)(const char *); /* A wrapper
							 * around getenv
							 */

	/* Launch FUSE mount.  Returns true on success */
	bool (*register_fuse_fs)(void *handle, struct fuse_operations*,
				 struct fuse_lowlevel_ops*,
				 struct fuse_args*,
				 const char *, bool, void *,
				 struct fuse_session**);

	/* Registers a variable, exported as a control file system file
	 * and associates optional callbacks with read and write events.
	 */
	int (*register_ctrl_variable)(struct ctrl_dir *dir,
				      const char *name,
				      ctrl_fs_read_cb_t read_cb,
				      ctrl_fs_write_cb_t write_cb,
				      ctrl_fs_destroy_cb_t destroy_cb,
				      void *cb_arg);
	/* Registers an event, exported as a control file system file
	 * and associates optional callbacks with change events.
	 */
	int (*register_ctrl_event)(struct ctrl_dir *dir, const char *name,
				   ctrl_fs_trigger_cb_t trigger_cb,
				   ctrl_fs_destroy_cb_t destroy_cb,
				   void *cb_arg);
	/* Registers a tracker, exported as a control file system file
	 * and associates optional callbacks with open/close events.
	 */
	int (*register_ctrl_tracker)(struct ctrl_dir *dir, const char *name,
				     ctrl_fs_open_cb_t open_cb,
				     ctrl_fs_close_cb_t close_cb,
				     ctrl_fs_destroy_cb_t destroy_cb,
				     void *cb_arg);
	/*
	 * Control fs constant registration.  Output should be what you want
	 * to see when you cat <path>.
	 */
	int (*register_ctrl_constant)(struct ctrl_dir *dir, const char *name,
				      const char *output);
	/* Control fs subdir creation */
	int (*create_ctrl_subdir)(struct ctrl_dir *dir, const char *name,
				  struct ctrl_dir **newdir);
	/* Wraps ctrl_register_constant for convenience of registering an
	 * integer constant
	 */
	int (*register_ctrl_constant_int64)(struct ctrl_dir *dir,
					    const char *name, int64_t value);
	/* Wraps ctrl_register_constant for convenience of registering an
	 * unsigned integer constant
	 */
	int (*register_ctrl_constant_uint64)(struct ctrl_dir *dir,
					     const char *name, uint64_t value);

	/* Wraps register_ctrl_variable for convenience of registering a file
	 * which can read and return integer values
	 */
	int (*register_ctrl_uint64_variable)(struct ctrl_dir *dir,
					     const char *name,
					     ctrl_fs_uint64_read_cb_t read_cb,
					     ctrl_fs_uint64_write_cb_t write_cb,
					     void *cb_arg);
	/* CPPR needs to be able to access the "global file system" so needs
	 * to enumerate over projection to be able to pick a destination and
	 * then access the struct fs_ops structure to be able to write to it
	 */
};

/* Function lookup table provided by plugin to CNSS. */
struct cnss_plugin {
	int version; /** Set to CNSS_PLUGIN_VERSION for startup checks */
	int require_service; /* Does the plugin need CNSS to be a service
			      * process set
			      */
	char *name;    /** Short string used to prefix log information */
	void *handle;  /** Handle passed back to all callback functions */
	int (*start)(void *, struct cnss_plugin_cb *,
		     size_t); /* Called once at startup, should return 0.
			       * If a non-zero code is returned then the plugin
			       * is disabled and no more callbacks are made.
			       */
	int (*post_start)(void *);

	/* Shutdown sequence:
	 * 1. stop_client_services called for each plugin
	 * 2. flush_client_services called for each plugin
	 * 3. If CNSS is a service set, execute a crt_barrier
	 * 4. stop_plugin_services called for each plugin
	 * 5. flush_plugin_services called for each plugin
	 * 6. destroy_plugin_data called for each plugin
	 */
	void (*stop_client_services)(void *); /* Indicates to plugin that no
					       * additional 3rd party requests
					       * are expected.
					       */
	void (*flush_client_services)(void *); /* Wait for all outstanding
						* requests to finish
						*/
	void (*stop_plugin_services)(void *); /* Indicates to plugin that no
					       * additional requests are
					       * expected from other plugins
					       */
	void (*flush_plugin_services)(void *); /* Wait for all outstanding
						* requests to finish
						*/
	void (*destroy_plugin_data)(void *); /* Shutdown is complete and memory
					      * associated with plugin can
					      * now be safely deallocated
					      */
	int (*deregister_fuse)(void *); /* Remove a previously registered fuse
					 * handle.  Called only if
					 * register_fuse_fs returned true
					 */
	void (*flush_fuse)(void *); /* Flush a previously registered fuse
				     * handle.  Called only if register_fuse_fs
				     * returned true
				     */
	void (*dump_log)(void *); /* Optional log dump */
};

/* At startup the CNSS process loads every library in a predefined
 * directory, and looks for a cnss_plugin_init() function in that
 * library.  This function should pass out a struct cnss_plugin and
 * a size, and return 0 on success.
 */
typedef int (*cnss_plugin_init_t)(struct cnss_plugin **fns, size_t *size);

/* The name of the init symbol defined in the plugin library */
#define CNSS_PLUGIN_INIT_SYMBOL "cnss_plugin_init"

/* Runtime version checking.
 * The plugin must define .version to this value or it will be disabled at
 * runtime.
 *
 * Additionally, offsets of members within cnss_plugin are checked at runtime so
 * it is safe to expand the API by appending new members, whilst maintaining
 * binary compatibility, however if any members are moved to different offsets
 * or change parameters or meaning then change this version to force a
 * re-compile of existing plugins.
 */
#define CNSS_PLUGIN_VERSION 0x10f00e

/* Library (interception library or CPPR Library) needs function to "attach" to
 * local CNSS by opening file in ctrl filesystem and be able to detect network
 * address
 *
 * iof will need to install a shared library which IL and CPPR library can use.
 */

#if defined(__cplusplus)
}
#endif

#endif /* __CNSS_PLUGIN_H__ */
