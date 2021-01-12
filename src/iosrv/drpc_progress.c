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
 * drpc_progress: dRPC Listener Progress
 *
 * The listener progress is executed on each poll cycle for a dRPC server
 * listening on a socket. This file includes the progress method, as well
 * as functions related to its supporting data structures.
 */

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
		return -DER_MISC;
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
		rc = -DER_MISC;
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

static void
free_call_ctx(struct drpc_call_ctx *ctx)
{
	drpc_close(ctx->session);
	drpc_call_free(ctx->call);
	drpc_response_free(ctx->resp);
	D_FREE(ctx);
}

/**
 * ULT to execute the dRPC handler and send the response back
 */
static void
drpc_handler_ult(void *call_ctx)
{
	int			rc;
	struct drpc_call_ctx	*ctx = (struct drpc_call_ctx *)call_ctx;

	D_INFO("dRPC handler ULT for module=%u method=%u\n",
	       ctx->call->module, ctx->call->method);

	ctx->session->handler(ctx->call, ctx->resp);

	rc = drpc_send_response(ctx->session, ctx->resp);
	if (rc != 0)
		D_ERROR("Failed to send dRPC response (module=%u method=%u)\n",
			ctx->call->module, ctx->call->method);

	/*
	 * We are responsible for cleaning up the call ctx.
	 */
	free_call_ctx(ctx);
}

static struct drpc_call_ctx *
create_call_ctx(struct drpc *session_ctx, Drpc__Call *call,
		Drpc__Response *resp)
{
	struct drpc_call_ctx	*call_ctx;
	int			rc;

	D_ALLOC_PTR(call_ctx);
	if (call_ctx == NULL)
		return NULL;

	rc = drpc_add_ref(session_ctx);
	D_ASSERTF(rc == 0, "Couldn't add ref to dRPC session context");

	call_ctx->session = session_ctx;
	call_ctx->call = call;
	call_ctx->resp = resp;

	return call_ctx;
}

static int
handle_incoming_call(struct drpc *session_ctx)
{
	int			rc;
	Drpc__Call		*call = NULL;
	Drpc__Response		*resp = NULL;
	struct drpc_call_ctx	*call_ctx;

	rc = drpc_recv_call(session_ctx, &call);
	/* Need to respond even if it was a bad call */
	if (rc != 0 && rc != -DER_PROTO)
		return rc;

	resp = drpc_response_create(call);
	if (resp == NULL) {
		D_ERROR("Could not allocate Drpc__Response\n");
		drpc_call_free(call);
		return -DER_NOMEM;
	}

	/* Incoming message was garbage */
	if (rc == -DER_PROTO) {
		resp->status = DRPC__STATUS__FAILED_UNMARSHAL_CALL;
		drpc_send_response(session_ctx, resp);
		drpc_response_free(resp);
		return rc;
	}

	/*
	 * Call and response become part of the call context - freeing will
	 * be handled by the ULT.
	 */
	call_ctx = create_call_ctx(session_ctx, call, resp);
	if (call_ctx == NULL) {
		drpc_call_free(call);
		drpc_response_free(resp);
		return -DER_NOMEM;
	}

	/*
	 * Ownership of the call context is passed on to the handler ULT.
	 */
	rc = dss_ult_create(drpc_handler_ult, (void *)call_ctx,
			    DSS_XS_SYS, 0, 0, NULL);
	if (rc != 0) {
		D_ERROR("Failed to create drpc handler ULT: "DF_RC"\n",
			DP_RC(rc));
		free_call_ctx(call_ctx);
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

int
drpc_progress(struct drpc_progress_context *ctx, int timeout_ms)
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
			"structures, rc="DF_RC"\n", DP_RC(rc));
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
