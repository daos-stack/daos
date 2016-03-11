/**
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the GNU Lesser General Public License
 * (LGPL) version 2.1 which accompanies this distribution, and is available at
 * http://www.gnu.org/licenses/lgpl-2.1.html
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the LGPL License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 *
 * (C) Copyright 2016 Intel Corporation.
 */
/**
 * DAOS server-side infrastructure
 * Provides a modular interface to load server-side code on demand
 */

#ifndef __DSS_API_H__
#define __DSS_API_H__

/**
 * Stackable Module API
 * Provides a modular interface to load and register server-side code on
 * demand. A module is composed of:
 * - a set of request handlers which are registered when the module is loaded.
 * - a server-side API (see header files suffixed by "_srv") used for
 *   inter-module direct calls.
 *
 * For now, all loaded modules are assumed to be trustful, but sandboxes can be
 * implemented in the future.
 */

/**
 * Handler for a given type of incoming RPCs.
 */
struct dss_handler {
	/* Name of the handler */
	const char	 *sh_name;
	/* Operation code associated with the handler */
	int		  sh_opc;
	/* Request version for this operation code */
	int		  sh_ver;
	/* Operation flags, TBD */
	int		  sh_flags;
	/* Actual handler function */
	int		(*sh_func)(void *req);
};

/**
 * Each module should provide a dss_module structure which defines the module
 * interface. The name of the allocated structure must be the library name
 * (without the extension) suffixed by "handlers". This symbol will be looked up
 * automatically when the module library is loaded and failed if not found.
 */
struct dss_module {
	/* Name of the module */
	const char		 *sm_name;
	/* Module version */
	int			  sm_ver;
	/* Setup function, invoked just after successful load */
	int			(*sm_init)(void);
	/* Teardown function, invoked just before module unload */
	int			(*sm_fini)(void);
	/* Array of request handlers for RPC sent by client nodes */
	struct dss_handler	 *sm_cl_hdlrs;
	/* Array of request handlers for RPC sent by other servers */
	struct dss_handler	 *sm_srv_hdlrs;
};

/**
 * Request context
 */

#endif /* __DSS_API_H__ */
