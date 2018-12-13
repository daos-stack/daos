/*
 * (C) Copyright 2018 Intel Corporation.
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
 * ds_drpc: dRPC server library internal declarations
 */

#ifndef __DAOS_DRPC_INTERNAL_H__
#define __DAOS_DRPC_INTERNAL_H__

#include <daos/drpc.h>
#include <gurt/list.h>

/**
 * Context for listener's drpc_progress loop. Includes the context for the
 * listener, and a list of contexts for all open sessions.
 */
struct drpc_progress_context {
	struct drpc *listener_ctx; /** Just a pointer, not a copy */
	d_list_t session_ctx_list; /** Head of the session list */
};

/**
 * Simple linked list node containing a drpc context.
 * Used for the session_ctx_list in drpc_progress_context.
 */
struct drpc_list {
	struct drpc *ctx; /** Just a pointer, not a copy */
	d_list_t link; /** Linked list metadata */
};


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
 * \return	DER_SUCCESS		Successfully processed activity
 *		-DER_INVAL		Invalid ctx
 *		-DER_TIMEDOUT		No activity
 *		-DER_AGAIN		Couldn't process activity, try again
 *		-DER_NOMEM		Out of memory
 *		-DER_UNKNOWN		Unexpected error
 */
int drpc_progress(struct drpc_progress_context *ctx, int timeout);

#endif /* __DAOS_DRPC_INTERNAL_H__ */
