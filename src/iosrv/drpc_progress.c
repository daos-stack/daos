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

#include <abt.h>
#include <stdlib.h>
#include <poll.h>
#include <errno.h>
#include <daos_srv/daos_server.h>
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

/**
 * Context for an individual dRPC call.
 */
struct drpc_call_ctx {
	struct drpc	*ctx;
	Drpc__Call	*call;
	Drpc__Response	*resp;
	d_list_t	link; /* linked list metadata */
};

ABT_mutex drpc_pending_resp_mutex;
d_list_t drpc_pending_resp_list;

struct drpc_progress_context *
drpc_progress_context_create(struct drpc *listener)
{
	struct drpc_progress_context *result;

	if (!drpc_is_valid_listener(listener)) {
		D_ERROR("Invalid dRPC listener\n");
		return NULL;
	}

	D_ALLOC_PTR(result);
	if (result == NULL)
		return NULL;

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
		D_ERROR("NULL drpc_progress_context passed\n");
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

	if (event_bits & POLLHUP)
		activity = UNIXCOMM_ACTIVITY_PEER_DISCONNECTED;
	else if (event_bits & POLLERR)
		activity = UNIXCOMM_ACTIVITY_ERROR;
	else if (event_bits & POLLIN)
		activity = UNIXCOMM_ACTIVITY_DATA_IN;

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
	if (poll_rc == 0)
		return -DER_TIMEDOUT;

	if (poll_rc < 0) {
		D_ERROR("Polling failed, errno=%u\n", errno);
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
			D_ERROR("drpc_progress_context session ctx is not a "
				"valid listener\n");
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
		D_ERROR("Failed to count open drpc sessions\n");
		return num_comms;
	}

	/* include the listener in the count */
	num_comms += 1;

	D_ALLOC_ARRAY(new_comms, num_comms);
	if (new_comms == NULL)
		return -DER_NOMEM;

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
		D_ERROR("Failed to accept new drpc connection\n");
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
	int			rc = 0;
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
		D_INFO("Ignoring surprising listener activity: %u\n",
		       listener_comm->activity);
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

/**
 * ULT to execute the dRPC handler
 */
static int
drpc_handler_ult(void *call_ctx)
{
	struct drpc_call_ctx *ctx = (struct drpc_call_ctx *)call_ctx;

	D_INFO("dRPC handler ULT for module=%u method=%u\n",
	       ctx->call->module, ctx->call->method);
	ctx->ctx->handler(ctx->call, ctx->resp);

	ABT_mutex_lock(drpc_pending_resp_mutex);
	d_list_add_tail(&ctx->link, &drpc_pending_resp_list);
	ABT_mutex_unlock(drpc_pending_resp_mutex);

	return 0;
}

static int
handle_incoming_call(struct drpc *session_ctx)
{
	int			rc;
	Drpc__Call		*call = NULL;
	Drpc__Response		*resp;
	struct drpc_call_ctx	*call_ctx;

	rc = drpc_recv_call(session_ctx, &call);
	if (rc != 0)
		return rc;

	resp = drpc_response_create(call);
	if (resp == NULL) {
		D_ERROR("Could not allocate Drpc__Response\n");
		drpc_call_free(call);
		return -DER_NOMEM;
	}

	D_ALLOC_PTR(call_ctx);
	if (call_ctx == NULL) {
		D_ERROR("Could not allocate call context\n");
		drpc_call_free(call);
		drpc_response_free(resp);
		return -DER_NOMEM;
	}

	/*
	 * Ownership of the call context is passed on to the handler thread.
	 */
	call_ctx->ctx = session_ctx;
	call_ctx->call = call;
	call_ctx->resp = resp;
	D_INIT_LIST_HEAD(&call_ctx->link);

	rc = dss_ult_create_execute(drpc_handler_ult, (void *)call_ctx,
				    NULL, NULL,
				    DSS_ULT_DRPC_HANDLER, 0, 0);
	if (rc != 0) {
		D_ERROR("Failed to create drpc handler ULT: %d\n", rc);
		drpc_call_free(call);
		drpc_response_free(resp);
		D_FREE(call_ctx);
		return rc;
	}

	return 0;
}

static int
process_session_activity(struct drpc_list *session_node,
		struct unixcomm_poll *session_comm)
{
	int rc = 0;

	D_ASSERT(session_comm->comm->fd == session_node->ctx->comm->fd);

	switch (session_comm->activity) {
	case UNIXCOMM_ACTIVITY_DATA_IN:
		rc = handle_incoming_call(session_node->ctx);
		if (rc != 0 && rc != -DER_AGAIN) {
			D_ERROR("Error processing incoming session %u data\n",
				session_comm->comm->fd);
			destroy_session_node(session_node);

			/* No further action needed */
			rc = 0;
		}
		break;

	case UNIXCOMM_ACTIVITY_ERROR:
	case UNIXCOMM_ACTIVITY_PEER_DISCONNECTED:
		D_INFO("Session %u connection has been terminated\n",
		       session_comm->comm->fd);
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
	int			rc = 0;
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
		if (rc == 0)
			rc = session_rc;

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
	/* Only overwrite if there wasn't a previous session error */
	if (rc == 0)
		rc = listener_rc;

	return rc;
}

static int
progress_poll(struct drpc_progress_context *ctx, int timeout_ms)
{
	size_t			num_comms;
	struct unixcomm_poll	*comms;
	int			rc;

	if (!drpc_progress_context_is_valid(ctx)) {
		D_ERROR("Invalid drpc_progress_context\n");
		return -DER_INVAL;
	}

	rc = drpc_progress_context_to_unixcomms(ctx, &comms);
	if (rc < 0) {
		D_ERROR("Failed to convert drpc_progress_context to unixcomm "
			"structures, rc=%d\n", rc);
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

static int
send_queued_responses(void)
{
	int			rc = 0;
	int			tmp_rc;
	struct drpc_call_ctx	*current;
	struct drpc_call_ctx	*next;

	ABT_mutex_lock(drpc_pending_resp_mutex);
	d_list_for_each_entry_safe(current, next,
				   &drpc_pending_resp_list, link) {
		tmp_rc = drpc_send_response(current->ctx, current->resp);
		/*
		 * -DER_AGAIN implies a retry might succeed. In that case
		 * we won't clean up this item just yet.
		 */
		if (tmp_rc == -DER_AGAIN)
			continue;

		/* Only preserve the first error */
		if (rc == 0)
			rc = tmp_rc;

		/*
		 * Losing the ref to the call ctx, so it's time to clean it up.
		 * The drpc session is managed by the drpc_progress_context, but
		 * all other members need to be freed now.
		 */
		d_list_del(&current->link);
		drpc_call_free(current->call);
		drpc_response_free(current->resp);
		D_FREE(current);
	}
	ABT_mutex_unlock(drpc_pending_resp_mutex);

	return rc;
}

int
drpc_progress(struct drpc_progress_context *ctx, int timeout_ms)
{
	int rc;
	int tmp_rc;

	rc = progress_poll(ctx, timeout_ms);
	tmp_rc = send_queued_responses();
	if (rc == 0)
		rc = tmp_rc;

	return rc;
}

int
drpc_progress_init(void)
{
	int rc;

	D_INIT_LIST_HEAD(&drpc_pending_resp_list);

	rc = ABT_mutex_create(&drpc_pending_resp_mutex);
	if (rc != ABT_SUCCESS) {
		D_ERROR("Failed to create mutex\n");
		return dss_abterr2der(rc);
	}

	return 0;
}

int
drpc_progress_fini(void)
{
	int rc;

	/* TODO: clean up queue if there's anything left in it */

	rc = ABT_mutex_free(&drpc_pending_resp_mutex);
	if (rc != ABT_SUCCESS) {
		D_ERROR("ABT error freeing mutex: %d\n", rc);
		return dss_abterr2der(rc);
	}

	return 0;
}
