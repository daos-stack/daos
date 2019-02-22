/*
 * (C) Copyright 2018-2019 Intel Corporation.
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
 * drpc_progress: dRPC Listener Progress
 *
 * The listener progress is executed on each poll cycle for a dRPC server
 * listening on a socket. This file includes the progress method, as well
 * as functions related to its supporting data structures.
 */

#include <stdlib.h>
#include <poll.h>
#include <errno.h>
#include "drpc_internal.h"

/**
 * Interesting activities that could be seen on a unix domain socket.
 * Used in polling for activity.
 */
enum unixcomm_activity {
	UNIXCOMM_ACTIVITY_NONE,
	UNIXCOMM_ACTIVITY_DATA_IN,
	UNIXCOMM_ACTIVITY_PEER_DISCONNECTED,
	UNIXCOMM_ACTIVITY_ERROR
};

struct unixcomm_poll {
	struct unixcomm		*comm;
	enum unixcomm_activity	activity;
};

struct drpc_progress_context *
drpc_progress_context_create(struct drpc *listener)
{
	struct drpc_progress_context *result;

	if (!drpc_is_valid_listener(listener)) {
		return NULL;
	}

	D_ALLOC_PTR(result);
	if (result == NULL) {
		return NULL;
	}

	result->listener_ctx = listener;
	D_INIT_LIST_HEAD(&result->session_ctx_list);

	return result;
}

void
drpc_progress_context_close(struct drpc_progress_context *ctx)
{
	struct drpc_list	*current;
	struct drpc_list	*next;

	if (ctx == NULL) {
		return;
	}

	d_list_for_each_entry_safe(current, next, &ctx->session_ctx_list,
			link) {
		d_list_del(&current->link);
		drpc_close(current->ctx);
		D_FREE(current);
	}

	drpc_close(ctx->listener_ctx);

	D_FREE(ctx);
}

static enum unixcomm_activity
poll_events_to_unixcomm_activity(int event_bits)
{
	enum unixcomm_activity activity = UNIXCOMM_ACTIVITY_NONE;

	if (event_bits & POLLHUP) {
		activity = UNIXCOMM_ACTIVITY_PEER_DISCONNECTED;
	} else if (event_bits & POLLERR) {
		activity = UNIXCOMM_ACTIVITY_ERROR;
	} else if (event_bits & POLLIN) {
		activity = UNIXCOMM_ACTIVITY_DATA_IN;
	}

	return activity;
}

static int
unixcomm_poll(struct unixcomm_poll *comms, size_t num_comms, int timeout_ms)
{
	struct pollfd	fds[num_comms];
	int		poll_rc;
	size_t		i;

	memset(fds, 0, sizeof(fds));

	for (i = 0; i < num_comms; i++) {
		fds[i].fd = comms[i].comm->fd;
		fds[i].events = POLLIN | POLLPRI;
	}

	poll_rc = poll(fds, num_comms, timeout_ms);
	if (poll_rc == 0) { /* timeout */
		return -DER_TIMEDOUT;
	}

	if (poll_rc < 0) { /* failure */
		return daos_errno2der(errno);
	}

	for (i = 0; i < num_comms; i++) {
		comms[i].activity = poll_events_to_unixcomm_activity(
				fds[i].revents);
	}

	return poll_rc;
}

/*
 * Count the valid drpc contexts in the drpc_progress_context session list.
 * Returns error if an invalid drpc session context is found.
 */
static int
get_open_drpc_session_count(struct drpc_progress_context *ctx)
{
	struct drpc_list	*current;
	int			num_comms = 0;

	d_list_for_each_entry(current, &ctx->session_ctx_list, link) {
		if (!drpc_is_valid_listener(current->ctx)) {
			return -DER_INVAL;
		}

		num_comms++;
	}

	return num_comms;
}

/*
 * Convert a drpc_progress context to an array of unixcomm_poll structs.
 */
static int
drpc_progress_context_to_unixcomms(struct drpc_progress_context *ctx,
		struct unixcomm_poll **comms)
{
	struct drpc_list	*current;
	struct unixcomm_poll	*new_comms;
	int			num_comms;
	int			i;

	num_comms = get_open_drpc_session_count(ctx);
	if (num_comms < 0) {
		return num_comms;
	}

	/* include the listener in the count */
	num_comms += 1;

	D_ALLOC_ARRAY(new_comms, num_comms);
	if (new_comms == NULL) {
		return -DER_NOMEM;
	}

	i = 0;
	d_list_for_each_entry(current, &ctx->session_ctx_list, link) {
		new_comms[i].comm = current->ctx->comm;
		i++;
	}

	/* The listener should always be in the list */
	new_comms[i].comm = ctx->listener_ctx->comm;

	*comms = new_comms;
	return num_comms;
}

static bool
drpc_progress_context_is_valid(struct drpc_progress_context *ctx)
{
	return (ctx != NULL) && drpc_is_valid_listener(ctx->listener_ctx);
}

static int
drpc_progress_context_accept(struct drpc_progress_context *ctx)
{
	struct drpc		*session;
	struct drpc_list	*session_node;

	session = drpc_accept(ctx->listener_ctx);
	if (session == NULL) {
		/*
		 * Any failure to accept is weird and surprising
		 */
		return -DER_UNKNOWN;
	}

	D_ALLOC_PTR(session_node);
	if (session_node == NULL) {
		D_FREE(session);
		return -DER_NOMEM;
	}

	session_node->ctx = session;
	d_list_add(&session_node->link, &ctx->session_ctx_list);

	return DER_SUCCESS;
}

static int
process_listener_activity(struct drpc_progress_context *ctx,
		struct unixcomm_poll *comms, size_t num_comms)
{
	int			rc = DER_SUCCESS;
	size_t			last_idx = num_comms - 1;
	struct unixcomm_poll	*listener_comm = &(comms[last_idx]);
	/* Last comm is the listener */

	D_ASSERT(listener_comm->comm->fd == ctx->listener_ctx->comm->fd);

	switch (listener_comm->activity) {
	case UNIXCOMM_ACTIVITY_DATA_IN:
		rc = drpc_progress_context_accept(ctx);
		break;

	case UNIXCOMM_ACTIVITY_ERROR:
	case UNIXCOMM_ACTIVITY_PEER_DISCONNECTED:
		/* Unexpected - don't do anything */
		rc = -DER_UNKNOWN;
		break;

	default:
		break;
	}

	return rc;
}

static void
destroy_session_node(struct drpc_list *session_node)
{
	drpc_close(session_node->ctx);
	d_list_del(&session_node->link);
	D_FREE(session_node);
}

static int
process_session_activity(struct drpc_list *session_node,
		struct unixcomm_poll *session_comm)
{
	int rc = DER_SUCCESS;

	D_ASSERT(session_comm->comm->fd == session_node->ctx->comm->fd);

	switch (session_comm->activity) {
	case UNIXCOMM_ACTIVITY_DATA_IN:
		rc = drpc_recv(session_node->ctx);
		if (rc != DER_SUCCESS && rc != -DER_AGAIN) {
			destroy_session_node(session_node);

			/* No further action needed */
			rc = DER_SUCCESS;
		}
		break;

	case UNIXCOMM_ACTIVITY_ERROR:
	case UNIXCOMM_ACTIVITY_PEER_DISCONNECTED:
		/* connection is dead */
		destroy_session_node(session_node);
		break;

	default:
		break;
	}

	return rc;
}

static int
process_all_session_activities(struct drpc_progress_context *ctx,
		struct unixcomm_poll *comms, size_t num_comms)
{
	int			rc = DER_SUCCESS;
	struct drpc_list	*current;
	struct drpc_list	*next;
	size_t			i = 0;

	d_list_for_each_entry_safe(current, next,
			&ctx->session_ctx_list, link) {
		int session_rc;

		session_rc = process_session_activity(current, &(comms[i]));

		/*
		 * Only overwrite with first error.
		 * Keep trying other sessions.
		 */
		if (rc == DER_SUCCESS) {
			rc = session_rc;
		}

		i++;
	}

	return rc;
}

static int
process_activity(struct drpc_progress_context *ctx,
		struct unixcomm_poll *comms, size_t num_comms)
{
	int	rc;
	int	listener_rc;

	rc = process_all_session_activities(ctx, comms, num_comms);

	listener_rc = process_listener_activity(ctx, comms, num_comms);
	if (rc == DER_SUCCESS) {
		/* Only overwrite if there wasn't a session error */
		rc = listener_rc;
	}

	return rc;
}

int
drpc_progress(struct drpc_progress_context *ctx, int timeout_ms)
{
	size_t			num_comms;
	struct unixcomm_poll	*comms;
	int			rc;

	if (!drpc_progress_context_is_valid(ctx)) {
		return -DER_INVAL;
	}

	rc = drpc_progress_context_to_unixcomms(ctx, &comms);
	if (rc < 0) {
		return rc;
	}

	num_comms = rc;
	rc = unixcomm_poll(comms, num_comms, timeout_ms);
	if (rc > 0) {
		rc = process_activity(ctx, comms, num_comms);
	}

	D_FREE(comms);
	return rc;
}
