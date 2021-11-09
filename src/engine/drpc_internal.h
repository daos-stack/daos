/*
 * (C) Copyright 2018-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

/**
 * drpc_listener: dRPC Listener
 *
 * The dRPC listener is a thread that sets up a UNIX Domain Socket to listen for
 * local client connections and processes dRPC messages from those clients. It
 * is expected to stay alive for the life of the I/O Engine.
 */

#ifndef __DAOS_DRPC_INTERNAL_H__
#define __DAOS_DRPC_INTERNAL_H__

#include <daos/drpc.h>
#include <gurt/list.h>
#include <daos_srv/ras.h>

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
 */
void drpc_listener_fini(void);

/**
 * Fetch the socket path for the dRPC listener.
 *
 * @return	The path being used for the socket
 */
const char *drpc_listener_get_socket_path(void);

/** Initialize the dRPC client. */
int drpc_init(void);

/** Finalize the dRPC client. */
void drpc_fini(void);

int drpc_notify_ready(void);

#endif /* __DAOS_DRPC_INTERNAL_H__ */
