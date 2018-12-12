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

#include <daos/common.h>
#include <daos/drpc.h>
#include <daos/drpc.pb-c.h>
#include <gurt/errno.h>
#include <gurt/list.h>

#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <linux/un.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>

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
	struct unixcomm *comm;
	enum unixcomm_activity activity;
};

static int unixcomm_close(struct unixcomm *handle);

static struct unixcomm *
new_unixcomm_socket(int flags)
{
	struct unixcomm	*comm;

	D_ALLOC_PTR(comm);
	if (comm == NULL) {
		return NULL;
	}

	comm->fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
	if (comm->fd < 0) {
		D_FREE(comm);
		return NULL;
	}

	if (fcntl(comm->fd, F_SETFL, flags) < 0) {
		unixcomm_close(comm);
		return NULL;
	}

	comm->flags = flags;

	return comm;
}

static void
fill_socket_address(const char *sockpath, struct sockaddr_un *address)
{
	memset(address, 0, sizeof(struct sockaddr_un));

	address->sun_family = AF_UNIX;
	strncpy(address->sun_path, sockpath, UNIX_PATH_MAX-1);
}

static struct unixcomm *
unixcomm_connect(char *sockaddr, int flags)
{
	struct sockaddr_un	address;
	int			ret;
	struct unixcomm		*handle = NULL;

	handle = new_unixcomm_socket(flags);
	if (handle == NULL) {
		return NULL;
	}

	fill_socket_address(sockaddr, &address);
	ret = connect(handle->fd, (struct sockaddr *) &address,
			sizeof(address));
	if (ret < 0) {
		unixcomm_close(handle);
		return NULL;
	}

	return handle;
}

static struct unixcomm *
unixcomm_listen(char *sockaddr, int flags)
{
	struct sockaddr_un	address;
	struct unixcomm		*comm;

	comm = new_unixcomm_socket(flags);
	if (comm == NULL) {
		return NULL;
	}

	fill_socket_address(sockaddr, &address);
	if (bind(comm->fd, (struct sockaddr *)&address,
			sizeof(struct sockaddr_un)) < 0) {
		unixcomm_close(comm);
		return NULL;
	}

	if (listen(comm->fd, SOMAXCONN) < 0) {
		unixcomm_close(comm);
		return NULL;
	}

	return comm;
}

static struct unixcomm *
unixcomm_accept(struct unixcomm *listener)
{
	struct unixcomm *comm;

	D_ALLOC_PTR(comm);
	if (comm == NULL) {
		return NULL;
	}

	comm->fd = accept(listener->fd, NULL, NULL);
	if (comm->fd < 0) {
		/* couldn't get a connection */
		D_FREE(comm);
		return NULL;
	}

	return comm;
}

static int
unixcomm_close(struct unixcomm *handle)
{
	int ret;

	if (!handle)
		return 0;
	ret = close(handle->fd);
	D_FREE(handle);

	if (ret < 0) {
		return daos_errno2der(errno);
	}

	return 0;
}

static int
unixcomm_send(struct unixcomm *hndl, uint8_t *buffer, size_t buflen,
		ssize_t *sent)
{
	int		ret;
	struct iovec	iov[1];
	struct msghdr	msg;
	ssize_t		bsent;

	memset(&msg, 0, sizeof(msg));

	iov[0].iov_base = buffer;
	iov[0].iov_len = buflen;
	msg.msg_iov = iov;
	msg.msg_iovlen = 1;

	bsent = sendmsg(hndl->fd, &msg, 0);
	if (bsent < 0) {
		ret = daos_errno2der(errno);
	} else {
		if (sent != NULL) {
			*sent = bsent;
		}
		ret = 0;
	}
	return ret;
}

static int
unixcomm_recv(struct unixcomm *hndl, uint8_t *buffer, size_t buflen,
		ssize_t *rcvd)
{
	int		ret;
	struct iovec	iov[1];
	struct msghdr	msg;
	ssize_t		brcvd;

	memset(&msg, 0, sizeof(msg));

	iov[0].iov_base = buffer;
	iov[0].iov_len = buflen;
	msg.msg_iov = iov;
	msg.msg_iovlen = 1;

	brcvd = recvmsg(hndl->fd, &msg, 0);
	if (brcvd < 0) {
		ret = daos_errno2der(errno);
	} else {
		if (rcvd != NULL) {
			*rcvd = brcvd;
		}
		ret = 0;
	}
	return ret;
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

static Drpc__Response *
rpc_reply_create(int sequence, int status, int body_length, uint8_t *body)
{
	Drpc__Response *resp = NULL;

	D_ALLOC_PTR(resp);
	if (!resp) {
		return NULL;
	}
	drpc__response__init(resp);
	resp->sequence = sequence;
	resp->status = status;
	if (body) {
		resp->body.len = body_length;
		resp->body.data = body;
	}
	return resp;
}

static int
drpc_marshal_call(Drpc__Call *msg, uint8_t **bytes)
{
	int	buf_len;
	uint8_t	*buf;

	if (!msg) {
		return -DER_INVAL;
	}

	buf_len = drpc__call__get_packed_size(msg);

	D_ALLOC(buf, buf_len);
	if (!buf) {
		return -DER_NOMEM;
	}

	drpc__call__pack(msg, buf);

	*bytes = buf;
	return buf_len;
}

static Drpc__Response *
drpc_unmarshal_response(uint8_t *buff, size_t buflen)
{
	Drpc__Response *resp;

	resp = drpc__response__unpack(NULL, buflen, buff);
	return resp;
}

/**
 * Issue a call over a drpc channel
 *
 * \param ctx	Context representing the connection.
 * \param flags	Flags to specify specific call behavior (SYNC/ASYNC).
 * \param msg	The drpc call structure representing the function call.
 * \param resp	The drpc response structure for the call.
 *
 * \returns	On success returns 0 otherwise returns negative error condition.
 */
int
drpc_call(struct drpc *ctx, int flags, Drpc__Call *msg,
			Drpc__Response **resp)
{
	Drpc__Response	*response = NULL;
	uint8_t		*messagePb;
	uint8_t		*responseBuf;
	int		pbLen;
	ssize_t		sent;
	ssize_t		recv = 0;
	int		ret;

	msg->sequence = ctx->sequence++;
	pbLen = drpc_marshal_call(msg, &messagePb);
	if (pbLen < 0) {
		return -DER_NOMEM;
	}

	ret = unixcomm_send(ctx->comm, messagePb, pbLen, &sent);
	D_FREE(messagePb);

	if (ret < 0) {
		return ret;
	}

	if (!(flags & R_SYNC)) {
		response = rpc_reply_create(msg->sequence,
						DRPC__STATUS__SUBMITTED,
						0, NULL);
		*resp = response;
		return 0;
	}

	D_ALLOC(responseBuf, UNIXCOMM_MAXMSGSIZE);
	if (!responseBuf) {
		return -DER_NOMEM;
	}

	ret = unixcomm_recv(ctx->comm, responseBuf, UNIXCOMM_MAXMSGSIZE, &recv);
	if (ret < 0) {
		D_FREE(responseBuf);
		return ret;
	}
	response = drpc_unmarshal_response(responseBuf, recv);
	D_FREE(responseBuf);

	*resp = response;
	return 0;
}

/**
 * Connect to a drpc socket server on the given path.
 *
 * \param sockaddr	Path to unix domain socket in the filesystem.
 *
 * \returns		Drpc context representing the connection.
 */
struct drpc *
drpc_connect(char *sockaddr)
{
	struct drpc	*ctx;
	struct unixcomm	*comms;

	D_ALLOC_PTR(ctx);
	if (!ctx) {
		return NULL;
	}

	comms = unixcomm_connect(sockaddr, 0);
	if (!comms) {
		D_FREE(ctx);
		return NULL;
	}

	ctx->comm = comms;
	ctx->sequence = 0;
	ctx->handler = NULL;
	return ctx;
}

/**
 * Set up a drpc socket server to passively listen for connections on a given
 * path.
 *
 * \param	sockaddr	Path to unix domain socket in the filesystem
 * \param	handler		Handler for messages received on sessions for
 *					this socket.
 *
 * \return	drpc context representing the listener, or NULL if failed to
 *			create one
 */
struct drpc *
drpc_listen(char *sockaddr, void (*handler)(Drpc__Call *, Drpc__Response **))
{
	struct drpc *ctx;

	if (sockaddr == NULL || handler == NULL) {
		return NULL;
	}

	D_ALLOC_PTR(ctx);
	if (ctx == NULL) {
		return NULL;
	}

	ctx->comm = unixcomm_listen(sockaddr, O_NONBLOCK);
	if (ctx->comm == NULL) {
		D_FREE(ctx);
		return NULL;
	}

	ctx->handler = handler;

	return ctx;
}

static bool
is_valid_listener_ctx(struct drpc *ctx)
{
	/*
	 * Listener needs a handler or else it's pretty useless
	 */
	return (ctx != NULL) && (ctx->comm != NULL) && (ctx->handler != NULL);
}

/**
 * Wait for a client to connect to a listening drpc context, and return the
 * context for the client's session.
 *
 * \param	ctx	drpc context created by drpc_listen()
 *
 * \return	new drpc context for the accepted client session, or
 *			NULL if failed to get one
 */
struct drpc *
drpc_accept(struct drpc *listener_ctx)
{
	struct drpc *session_ctx;

	if (!is_valid_listener_ctx(listener_ctx)) {
		return NULL;
	}

	D_ALLOC_PTR(session_ctx);
	if (session_ctx == NULL) {
		return NULL;
	}

	session_ctx->comm = unixcomm_accept(listener_ctx->comm);
	if (session_ctx->comm == NULL) {
		D_FREE(session_ctx);
		return NULL;
	}

	return session_ctx;
}

static int
send_response(struct drpc *ctx, Drpc__Response *response)
{
	int	rc;
	size_t	buffer_len;
	uint8_t	*buffer;

	buffer_len = drpc__response__get_packed_size(response);

	D_ALLOC(buffer, buffer_len);
	if (buffer == NULL) {
		return -DER_NOMEM;
	}

	drpc__response__pack(response, buffer);
	rc = unixcomm_send(ctx->comm, buffer, buffer_len, NULL);

	D_FREE(buffer);
	return rc;
}

static int
handle_incoming_message(struct drpc *ctx, Drpc__Response **response)
{
	int		rc;
	uint8_t		buffer[UNIXCOMM_MAXMSGSIZE];
	ssize_t		message_len = 0;
	Drpc__Call	*request;

	rc = unixcomm_recv(ctx->comm, buffer, sizeof(buffer),
				&message_len);
	if (rc != DER_SUCCESS) {
		return rc;
	}

	request = drpc__call__unpack(NULL, message_len, buffer);
	if (request == NULL) {
		/* Couldn't unpack into a Drpc__Call */
		return -DER_MISC;
	}

	ctx->handler(request, response);
	if (*response == NULL) {
		/* Handler failed to allocate a response */
		return -DER_NOMEM;
	}

	drpc__call__free_unpacked(request, NULL);
	return DER_SUCCESS;
}

/**
 * Listen for a client message on a drpc session, handle the message, and send
 * the response back to the client.
 *
 * \param	ctx		drpc context on which to listen
 *
 * \return	DER_SUCCESS	Successfully got and handled the message
 *		-DER_INVAL	Invalid drpc session context
 *		-DER_NOMEM	Out of memory
 *		-DER_AGAIN	Listener socket is nonblocking and there was no
 *					pending message on the pipe.
 *		-DER_MISC	Error processing message
 */
int
drpc_recv(struct drpc *session_ctx)
{
	int		rc;
	Drpc__Response	*response = NULL;

	if (!is_valid_listener_ctx(session_ctx)) {
		return -DER_INVAL;
	}

	rc = handle_incoming_message(session_ctx, &response);
	if (rc != DER_SUCCESS) {
		return rc;
	}

	rc = send_response(session_ctx, response);

	drpc__response__free_unpacked(response, NULL);
	return rc;
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
		if (!is_valid_listener_ctx(current->ctx)) {
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
	return (ctx != NULL) && is_valid_listener_ctx(ctx->listener_ctx);
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

/**
 * Close the existing drpc connection.
 *
 * \param ctx	The drpc context representing the connection (will be freed)
 *
 * \returns	Whether the underlying close on the socket was successful.
 */
int
drpc_close(struct drpc *ctx)
{
	int ret;

	if (!ctx || !ctx->comm) {
		return -DER_INVAL;
	}

	ret = unixcomm_close(ctx->comm);
	D_FREE(ctx);
	return ret;
}
