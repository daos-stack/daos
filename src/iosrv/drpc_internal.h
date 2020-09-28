/*
 * (C) Copyright 2018-2020 Intel Corporation.
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
 * drpc_listener: dRPC Listener
 *
 * The dRPC listener is a thread that sets up a UNIX Domain Socket to listen for
 * local client connections and processes dRPC messages from those clients. It
 * is expected to stay alive for the life of the I/O server.
 */

#ifndef __DAOS_DRPC_INTERNAL_H__
#define __DAOS_DRPC_INTERNAL_H__

#include <daos/drpc.h>
#include <gurt/list.h>

/**
 * Path to the Unix Domain Socket used by the dRPC listener thread
 */
extern char *drpc_listener_socket_path;

/**
 * Context for listener's drpc_progress loop. Includes the context for the
 * listener, and a list of contexts for all open sessions.
 */
struct drpc_progress_context {
	struct drpc	*listener_ctx; /** Just a pointer, not a copy */
	d_list_t	session_ctx_list; /** Head of the session list */
};

/**
 * Context for an individual dRPC call.
 */
struct drpc_call_ctx {
	struct drpc	*session;
	Drpc__Call	*call;
	Drpc__Response	*resp;
};

/**
 * Simple linked list node containing a drpc context.
 * Used for the session_ctx_list in drpc_progress_context.
 */
struct drpc_list {
	struct drpc	*ctx; /** Just a pointer, not a copy */
	d_list_t	link; /** Linked list metadata */
};

/**
 * Create a new drpc_progress_context using a valid listener that is already
 * open on a socket.
 *
 * \param	listener	Valid drpc listener context
 *
 * \return	Newly allocated drpc_progress_context, or NULL if none created
 */
struct drpc_progress_context *
drpc_progress_context_create(struct drpc *listener);

/**
 * Close all open drpc contexts in the drpc_progress_context, including the
 * listener, and free the structure.
 *
 * \param	ctx		Valid drpc_progress_context
 */
void drpc_progress_context_close(struct drpc_progress_context *ctx);

/**
 * Check drpc contexts for activity, and handle that activity.
 *
 * Incoming messages are processed using the dRPC handler.
 * Incoming connections are added to the ctx session list.
 * Failed or closed connections are cleaned up and removed from the session
 *	list.
 *
 * \param[in][out]	ctx		Progress context, which includes the
 *						listener and all open sessions.
 * \param[in]		timeout_ms	Timeout in milliseconds. Negative value
 *						blocks forever.
 *
 * \return	0			Successfully processed activity
 *		-DER_INVAL		Invalid ctx
 *		-DER_TIMEDOUT		No activity
 *		-DER_AGAIN		Couldn't process activity, try again
 *		-DER_NOMEM		Out of memory
 *		-DER_MISC		Unexpected error
 */
int drpc_progress(struct drpc_progress_context *ctx, int timeout);

/**
 * Start up the dRPC listener thread.
 *
 * \return	0		Success
 *		-DER_NOMEM	Out of memory
 *		-DER_INVAL	Invalid internal state
 */
int drpc_listener_init(void);

/**
 * Shut down the dRPC listener thread and clean up any open connections.
 *
 * Waits for the listener thread to complete before returning.
 *
 * \return	0	Success
 */
int drpc_listener_fini(void);

/**
 * Fetch the socket path for the dRPC listener.
 *
 * @return	The path being used for the socket
 */
const char *drpc_listener_get_socket_path(void);

/** Initialize the dRPC client (dss_drpc_ctx). */
int drpc_init(void);

/** Finalize the dRPC client (dss_drpc_ctx). */
void drpc_fini(void);

#endif /* __DAOS_DRPC_INTERNAL_H__ */
