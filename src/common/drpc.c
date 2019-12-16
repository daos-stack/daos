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

#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <linux/un.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

static int unixcomm_close(struct unixcomm *handle);

static void *drpc_alloc(void *allocator_date, size_t size)
{
	void *buf;

	D_ALLOC(buf, size);
	return buf;
}

static void drpc_free(void *allocater_data, void *pointer)
{
	D_FREE(pointer);
}

/* Define a custom allocator so we can log and use fault injection
 * in the DPRC code.
 */
static ProtobufCAllocator alloc = {.alloc = drpc_alloc,
				   .free = drpc_free};

/**
 * Allocate and initialize a new dRPC call Protobuf structure for a given dRPC
 * context.
 *
 * \param	ctx	Active dRPC context
 * \param	module	Module ID for the new call
 * \param	method	Method ID for the new call
 *
 * \return	Newly allocated Drpc__Call, or NULL if it couldn't be allocated
 */
Drpc__Call *
drpc_call_create(struct drpc *ctx, int32_t module, int32_t method)
{
	Drpc__Call *call;

	if (ctx == NULL) {
		D_ERROR("Can't build a call from NULL context\n");
		return NULL;
	}

	D_ALLOC_PTR(call);
	if (call == NULL)
		return NULL;

	drpc__call__init(call);
	call->sequence = ctx->sequence;
	call->module = module;
	call->method = method;

	return call;
}

/**
 * Free a dRPC Call Protobuf structure.
 *
 * \param	call	dRPC call to be freed
 */
void
drpc_call_free(Drpc__Call *call)
{
	drpc__call__free_unpacked(call, &alloc);
}

/**
 * Allocate and initialize a new dRPC Response to a given dRPC call.
 *
 * \param	call	dRPC call to be responded to
 *
 * \return	Newly allocated Drpc__Response, or NULL if it couldn't be
 *			allocated.
 */
Drpc__Response *
drpc_response_create(Drpc__Call *call)
{
	Drpc__Response *resp;

	if (call == NULL) {
		D_ERROR("Can't build a response from a NULL call\n");
		return NULL;
	}

	D_ALLOC_PTR(resp);
	if (resp == NULL)
		return NULL;

	drpc__response__init(resp);
	resp->sequence = call->sequence;

	return resp;
}

/**
 * Free a dRPC Response Protobuf structure.
 *
 * \param	resp	dRPC response to be freed
 */
void
drpc_response_free(Drpc__Response *resp)
{
	drpc__response__free_unpacked(resp, &alloc);
}

static struct unixcomm *
new_unixcomm_socket(int flags)
{
	struct unixcomm	*comm;

	D_ALLOC_PTR(comm);
	if (comm == NULL)
		return NULL;

	comm->fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
	if (comm->fd < 0) {
		D_ERROR("Failed to open socket, errno=%d\n", errno);
		D_FREE(comm);
		return NULL;
	}

	if (fcntl(comm->fd, F_SETFL, flags) < 0) {
		D_ERROR("Failed to set flags on socket fd %d, errno=%d\n",
			comm->fd, errno);
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
	if (handle == NULL)
		return NULL;

	fill_socket_address(sockaddr, &address);
	ret = connect(handle->fd, (struct sockaddr *) &address,
			sizeof(address));
	if (ret < 0) {
		D_ERROR("Failed to connect to socket fd %d, errno=%d\n",
			handle->fd, errno);
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
	if (comm == NULL)
		return NULL;

	fill_socket_address(sockaddr, &address);
	if (bind(comm->fd, (struct sockaddr *)&address,
		 sizeof(struct sockaddr_un)) < 0) {
		D_ERROR("Failed to bind socket at '%.4096s', fd=%d, errno=%d\n",
			sockaddr, comm->fd, errno);
		unixcomm_close(comm);
		return NULL;
	}

	if (listen(comm->fd, SOMAXCONN) < 0) {
		D_ERROR("Failed to start listening on socket fd %d, errno=%d\n",
			comm->fd, errno);
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
	if (comm == NULL)
		return NULL;

	comm->fd = accept(listener->fd, NULL, NULL);
	if (comm->fd < 0) {
		D_ERROR("Failed to accept connection on listener fd %d, "
			"errno=%d\n", listener->fd, errno);
		D_FREE(comm);
		return NULL;
	}

	return comm;
}

static int
unixcomm_close(struct unixcomm *handle)
{
	int ret;
	int fd;

	if (!handle)
		return 0;

	fd = handle->fd;
	ret = close(fd);
	D_FREE(handle);

	if (ret < 0) {
		D_ERROR("Failed to close socket fd %d, errno=%d\n",
			fd, errno);
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
		D_ERROR("Failed to sendmsg on socket fd %d, errno=%d\n",
			hndl->fd, errno);
		ret = daos_errno2der(errno);
	} else {
		if (sent != NULL)
			*sent = bsent;
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
		D_ERROR("Failed to recvmsg on socket fd %d, errno=%d\n",
			hndl->fd, errno);
		ret = daos_errno2der(errno);
	} else {
		if (rcvd != NULL)
			*rcvd = brcvd;
		ret = 0;
	}
	return ret;
}

static int
drpc_marshal_call(Drpc__Call *msg, uint8_t **bytes)
{
	int	buf_len;
	uint8_t	*buf;

	if (!msg) {
		D_ERROR("NULL Drpc__Call\n");
		return -DER_INVAL;
	}

	buf_len = drpc__call__get_packed_size(msg);

	D_ALLOC(buf, buf_len);
	if (!buf)
		return -DER_NOMEM;

	drpc__call__pack(msg, buf);

	*bytes = buf;
	return buf_len;
}

static Drpc__Response *
drpc_unmarshal_response(uint8_t *buff, size_t buflen)
{
	Drpc__Response *resp;

	resp = drpc__response__unpack(&alloc, buflen, buff);
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
	if (pbLen < 0)
		return pbLen;

	ret = unixcomm_send(ctx->comm, messagePb, pbLen, &sent);
	D_FREE(messagePb);

	if (ret < 0)
		return ret;

	if (!(flags & R_SYNC)) {
		response = drpc_response_create(msg);
		response->status = DRPC__STATUS__SUBMITTED;
		*resp = response;
		return 0;
	}

	D_ALLOC(responseBuf, UNIXCOMM_MAXMSGSIZE);
	if (!responseBuf)
		return -DER_NOMEM;

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

static void
init_drpc_ctx(struct drpc *ctx, struct unixcomm *comm, drpc_handler_t handler)
{
	ctx->comm = comm;
	ctx->handler = handler;
	ctx->sequence = 0;
	ctx->ref_count = 1;
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
	if (!ctx)
		return NULL;

	comms = unixcomm_connect(sockaddr, 0);
	if (!comms) {
		D_FREE(ctx);
		return NULL;
	}

	init_drpc_ctx(ctx, comms, NULL);
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
drpc_listen(char *sockaddr, drpc_handler_t handler)
{
	struct drpc	*ctx;
	struct unixcomm *comm;

	if (sockaddr == NULL || handler == NULL) {
		D_ERROR("Bad input, sockaddr=%p, handler=%p\n",
			sockaddr, handler);
		return NULL;
	}

	D_ALLOC_PTR(ctx);
	if (ctx == NULL)
		return NULL;

	comm = unixcomm_listen(sockaddr, O_NONBLOCK);
	if (comm == NULL) {
		D_FREE(ctx);
		return NULL;
	}

	init_drpc_ctx(ctx, comm, handler);
	return ctx;
}

/**
 * Determines if the drpc ctx is set up as a listener.
 *
 * \param	ctx	Active drpc context
 *
 * \return	True if a valid listener, false otherwise
 */
bool
drpc_is_valid_listener(struct drpc *ctx)
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
	struct drpc	*session_ctx;
	struct unixcomm	*comm;

	if (!drpc_is_valid_listener(listener_ctx)) {
		D_ERROR("dRPC context is not a listener\n");
		return NULL;
	}

	D_ALLOC_PTR(session_ctx);
	if (session_ctx == NULL)
		return NULL;

	comm = unixcomm_accept(listener_ctx->comm);
	if (comm == NULL) {
		D_FREE(session_ctx);
		return NULL;
	}

	init_drpc_ctx(session_ctx, comm, listener_ctx->handler);
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
	if (buffer == NULL)
		return -DER_NOMEM;

	drpc__response__pack(response, buffer);
	rc = unixcomm_send(ctx->comm, buffer, buffer_len, NULL);

	D_FREE(buffer);
	return rc;
}

static int
get_incoming_call(struct drpc *ctx, Drpc__Call **call)
{
	int		rc;
	uint8_t		*buffer;
	size_t		buffer_size = UNIXCOMM_MAXMSGSIZE;
	ssize_t		message_len = 0;

	D_ALLOC(buffer, buffer_size);
	if (buffer == NULL)
		return -DER_NOMEM;

	rc = unixcomm_recv(ctx->comm, buffer, buffer_size,
				&message_len);
	if (rc != DER_SUCCESS) {
		D_FREE(buffer);
		return rc;
	}

	*call = drpc__call__unpack(&alloc, message_len, buffer);
	D_FREE(buffer);
	if (*call == NULL) {
		D_ERROR("Couldn't unpack message into Drpc__Call\n");
		return -DER_PROTO;
	}

	return 0;
}

/**
 * Listen for a client message on a dRPC session and return the Drpc__Call
 * received.
 *
 * \param[in]	session_ctx	Valid dRPC session context
 * \param[out]	call		Newly allocated Drpc__Call
 *
 * \return	0		Successfully got a message
 *		-DER_INVAL	Invalid input
 *		-DER_NOMEM	Out of memory
 *		-DER_AGAIN	Listener socket is nonblocking and there was no
 *				pending message on the pipe
 *		-DER_PROTO	Badly-formed incoming message
 */
int
drpc_recv_call(struct drpc *session_ctx, Drpc__Call **call)
{
	if (call == NULL) {
		D_ERROR("Call pointer is NULL\n");
		return -DER_INVAL;
	}

	if (!drpc_is_valid_listener(session_ctx)) {
		D_ERROR("dRPC context isn't a valid listener\n");
		return -DER_INVAL;
	}

	return get_incoming_call(session_ctx, call);
}

/**
 * Send a given Drpc__Response to the client on a dRPC session.
 *
 * \param[in]	ctx	Valid dRPC session context
 * \param[in]	resp	Response to be sent
 *
 * \return	0		Successfully sent message
 *		-DER_INVAL	Invalid input
 *		-DER_NOMEM	Out of memory
 *		-DER_NO_PERM	Bad socket permissions
 *		-DER_NO_HDL	Invalid socket fd in ctx
 *		-DER_AGAIN	Operation blocked, try again
 *		-DER_MISC	Miscellaneous error sending response
 */
int
drpc_send_response(struct drpc *session_ctx, Drpc__Response *resp)
{
	if (resp == NULL) {
		D_ERROR("Response was NULL\n");
		return -DER_INVAL;
	}

	if (!drpc_is_valid_listener(session_ctx)) {
		D_ERROR("dRPC context isn't a valid listener\n");
		return -DER_INVAL;
	}

	return send_response(session_ctx, resp);
}

/**
 * Close the existing drpc connection.
 *
 * If there are multiple references to the context, the ref count will be
 * decremented. Otherwise the context will be freed.
 *
 * \param ctx	The drpc context representing the connection (will be freed if
 *		last reference is removed)
 *
 * \returns	Whether the underlying close on the socket was successful.
 */
int
drpc_close(struct drpc *ctx)
{
	int		ret;
	uint32_t	new_count;

	if (!ctx || !ctx->comm) {
		D_ERROR("Context is already closed\n");
		return -DER_INVAL;
	}

	if (ctx->ref_count == 0) {
		D_ERROR("Ref count is already zero\n");
		return -DER_INVAL;
	}

	D_DEBUG(DB_MGMT, "Decrementing refcount (%u)\n",
		ctx->ref_count);
	ctx->ref_count--;

	new_count = ctx->ref_count;

	if (new_count == 0) {
		D_INFO("Closing dRPC socket fd=%d\n", ctx->comm->fd);

		ret = unixcomm_close(ctx->comm);
		if (ret != 0)
			D_ERROR("Failed to close dRPC socket (rc=%d)\n", ret);
		D_FREE(ctx);
	}
	return 0;
}

/**
 * Adds to the reference count of the dRPC context.
 *
 * \param	ctx	dRPC context
 *
 * \return	0		Success
 *		-DER_INVAL	Context is not valid
 */
int
drpc_add_ref(struct drpc *ctx)
{
	int rc = 0;

	if (ctx == NULL) {
		D_ERROR("Context is null\n");
		return -DER_INVAL;
	}

	if (ctx->ref_count == UINT_MAX) {
		D_ERROR("Can't increment current ref count (count=%u)\n",
			ctx->ref_count);
		D_GOTO(out, rc = -DER_INVAL);
	}

	ctx->ref_count++;
out:
	return rc;
}

