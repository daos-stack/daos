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
#include <stdio.h>

static struct unixcomm *
unixcomm_connect(char *sockaddr, int flags)
{
	struct sockaddr_un	address;
	int			fd;
	int			ret;
	struct unixcomm		*handle = NULL;

	D_ALLOC_PTR(handle);
	if (!handle)
		return NULL;

	address.sun_family = AF_UNIX;
	strncpy(address.sun_path, sockaddr, UNIX_PATH_MAX-1);

	fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
	if (fd < 0) {
		D_FREE(handle);
		return NULL;
	}

	ret = connect(fd, (struct sockaddr *) &address, sizeof(address));
	if (ret < 0) {
		D_FREE(handle);
		return NULL;
	}

	handle->fd = fd;
	handle->flags = flags;
	return handle;
}

static int
unixcomm_close(struct unixcomm *handle)
{
	int ret;

	if (!handle)
		return 0;
	ret = close(handle->fd);
	D_FREE_PTR(handle);

	if (ret < 0) {
		return daos_errno2der(errno);
	} else {
		return 0;
	}
}

static int
unixcomm_send(struct unixcomm *hndl, char *buffer, size_t buflen, ssize_t *sent)
{
	int		ret;
	struct iovec	iov[1];
	struct msghdr	msg;
	ssize_t		bsent;

	iov[0].iov_base = buffer;
	iov[0].iov_len = buflen;
	msg.msg_iov = iov;
	msg.msg_iovlen = 1;

	bsent = sendmsg(hndl->fd, &msg, 0);
	if (bsent < 0) {
		ret = daos_errno2der(errno);
	} else {
		*sent = bsent;
		ret = 0;
	}
	return ret;
}

static int
unixcomm_recv(struct unixcomm *hndl, char *buffer, size_t buflen, ssize_t *rcvd)
{
	int		ret;
	struct iovec	iov[1];
	struct msghdr	msg;
	ssize_t		brcvd;

	iov[0].iov_base = buffer;
	iov[0].iov_len = buflen;
	msg.msg_iov = iov;
	msg.msg_iovlen = 1;

	brcvd = recvmsg(hndl->fd, &msg, 0);
	if (brcvd < 0) {
		ret = daos_errno2der(errno);
	} else {
		*rcvd = brcvd;
		ret = 0;
	}
	return ret;
}

static Drpc__Response *
rpc_reply_create(int sequence, int status, int body_length, char *body)
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
		resp->body.data = (uint8_t *)body;
	}
	return resp;
}

static int
drpc_marshal_call(Drpc__Call *msg, char **bytes)
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

	*bytes = (char *)buf;
	return buf_len;
}

static Drpc__Response *
drpc_unmarshal_response(char *buff, int buflen)
{
	Drpc__Response *resp;

	resp = drpc__response__unpack(NULL, buflen, (uint8_t *)buff);
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
	char		*messagePb;
	char		*responseBuf;
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
		D_FREE_PTR(ctx);
		return NULL;
	}

	ctx->comm = comms;
	ctx->sequence = 0;
	return ctx;
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
	D_FREE_PTR(ctx);
	return ret;
}
