/*
 * (C) Copyright 2019 Intel Corporation.
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
 * provided in Contract No. 8F-30005.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */

/**
 * drpc_hdlr: dRPC Handler Registry
 *
 * The dRPC Handler Registry handles the registration and lookup of dRPC message
 * handler functions based on the dRPC module ID.
 *
 * Incoming dRPC messages (Drpc__Call) include the dRPC module ID, which allows
 * us to look up the associated handler for the message. Each handler function
 * is expected to parse the Drpc__Call message, take any appropriate actions,
 * and allocate a new Drpc__Response to return to the dRPC client. The handler
 * should ALWAYS return some type of response, even if there was an error.
 */

#ifndef __DAOS_DRPC_HANDLER_H__
#define __DAOS_DRPC_HANDLER_H__

#include <daos/drpc.h>
#include <daos_srv/daos_server.h>

/**
 * Initialize the dRPC registry.
 *
 * @return	0		Success
 *		-DER_NOMEM	Out of memory
 */
int drpc_hdlr_init(void);

/**
 * Shutdown the dRPC registry.
 *
 * @return	0		Success
 */
int drpc_hdlr_fini(void);

/**
 * Register the dRPC handler for a given dRPC module.
 *
 * \param	module_id	ID for the dRPC messaging module
 * \param	handler		Handler function for the given module.
 *					If NULL this is a no-op.
 *
 * \return	0		Success
 *		-DER_NOMEM	Out of memory
 *		-DER_UNINIT	Not initialized
 *		-DER_EXIST	Module ID already registered
 */
int drpc_hdlr_register(int module_id, drpc_handler_t handler);

/**
 * Register a list of dRPC handlers.
 *
 * \param	handlers	Array of drpc_handler structs. Last one should
 *					be zeroed to indicate termination of
 *					list.
 *
 * \return	0		Success
 *		-DER_NOMEM	Out of memory
 *		-DER_UNINIT	Not initialized
 *		-DER_EXIST	Module ID already registered
 */
int drpc_hdlr_register_all(struct dss_drpc_handler *handlers);

/**
 * Get the appropriate dRPC handler for the module ID.
 *
 * \param	module_id	ID for the dRPC messaging module
 *
 * \return	Module's registered handler if it exists, NULL otherwise
 */
drpc_handler_t drpc_hdlr_get_handler(int module_id);

/**
 * Unregister the dRPC handler for a given dRPC messaging module.
 *
 * \param	module_id	ID for the dRPC messaging module
 *
 * \return	0		Success
 *		-DER_UNINIT	Not initialized
 */
int drpc_hdlr_unregister(int module_id);

/**
 * Unregister all dRPC handlers in a list.
 *
 * \param	handlers	Array of drpc_handler structs. Last one should
 *					be zeroed to indicate termination of
 *					list.
 *
 * \return	0		Success
 *		-DER_UNINIT	Not initialized
 */
int drpc_hdlr_unregister_all(struct dss_drpc_handler *handlers);

/**
 * Process the incoming request using the handler appropriate to its module ID.
 *
 * If the request is invalid or has no handler registered, the response reflects
 * this.
 *
 * \param[in]	request	Incoming Drpc__Call
 * \param[out]	resp	Result of processing the call
 */
void drpc_hdlr_process_msg(Drpc__Call *request, Drpc__Response *resp);

#endif /* __DAOS_DRPC_HANDLER_H__ */
